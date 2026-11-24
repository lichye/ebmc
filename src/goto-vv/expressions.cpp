#include <verilog/sva_expr.h>

#include "lower.h"

#include <bit>

namespace vv
{
namespace
{
exprt index_constant(uint64_t n)
{
  return from_integer(n, integer_typet());
}
exprt slice(exprt base, exprt hi, exprt lo, const typet &t)
{
  return node(
    ID_verilog_non_indexed_part_select,
    t,
    {std::move(base), std::move(hi), std::move(lo)});
}

std::pair<uint64_t, uint64_t> packed_slice_layout(
  const PackedArrayType &array,
  ConstantRange selection)
{
  const auto stride = array.elementType.getBitWidth();
  const auto first = array.range.translateIndex(selection.left);
  const auto last = array.range.translateIndex(selection.right);
  return {
    uint64_t(std::min(first, last)) * stride,
    uint64_t(selection.fullWidth()) * stride};
}

exprt packed_slice(
  exprt base,
  const PackedArrayType &array,
  ConstantRange selection,
  const typet &type)
{
  const auto [offset, width] = packed_slice_layout(array, selection);
  return slice(
    std::move(base),
    index_constant(offset + width - 1),
    index_constant(offset),
    type);
}

bool covers_packed_dimension(const Type &selector, ConstantRange range)
{
  const auto width = selector.getBitWidth();
  // Slang's declared bounds are int32_t. No wider domain can match them.
  if(!selector.isSimpleBitVector() || width == 0 || width > 32)
    return false;
  const bool is_signed = selector.isSigned();
  const auto value_bits = is_signed ? width - 1 : width;
  const auto limit = int64_t{1} << value_bits;
  return range.lower() == (is_signed ? -limit : 0) &&
         range.upper() == limit - 1;
}

bool modulo_covers_packed_dimension(
  const Expression &selector,
  ConstantRange range,
  const Symbol &scope)
{
  const auto *modulo = selector.as_if<BinaryExpression>();
  const auto is_unsigned = [](const Type &type)
  { return type.isIntegral() && !type.isSigned(); };
  if(
    range.lower() != 0 || !modulo || modulo->op != BinaryOperator::Mod ||
    !is_unsigned(*selector.type) || !is_unsigned(*modulo->left().type) ||
    !is_unsigned(*modulo->right().type))
    return false;

  EvalContext context(scope);
  const auto divisor = modulo->right().eval(context);
  // An unsigned remainder by the positive bank count is in [0, count-1].
  return divisor.isInteger() &&
         divisor.integer().as<uint64_t>() == range.fullWidth();
}

exprt packed_element_offset(const PackedArrayType &array, exprt index)
{
  auto type = index.type();
  auto right = from_integer(array.range.right, type);
  auto offset = array.range.isDescending()
                  ? minus_exprt(std::move(index), std::move(right))
                  : minus_exprt(std::move(right), std::move(index));
  return mult_exprt(
    std::move(offset), from_integer(array.elementType.getBitWidth(), type));
}

typet full_packed_offset_type(
  const PackedArrayType &array,
  const Type &selector)
{
  const auto width = std::bit_width(array.getBitWidth()) + 1;
  return selector.isSigned() ? typet(signedbv_typet(width))
                             : typet(unsignedbv_typet(width));
}

exprt as_sequence(exprt e)
{
  if(e.type().id() == ID_bool)
    return node(ID_sva_boolean, verilog_sva_sequence_typet(), {std::move(e)});
  return e;
}
exprt as_property(exprt e)
{
  if(e.type().id() == ID_verilog_sva_sequence)
    return node(
      ID_sva_sequence_property, verilog_sva_property_typet(), {std::move(e)});
  return e;
}
} // namespace

ConstantValue compilert::eval_known(const Expression &e) const
{
  if(!procedural_locals.empty())
    e.visitSymbolReferences(
      [&](const Expression &reference, const Symbol &symbol)
      {
        const auto local = procedural_locals.find(&symbol);
        if(local != procedural_locals.end() && !local->second.assigned)
          fail(
            reference.sourceRange.start(),
            std::string(
              local->second.require_full_assignment ? "combinational local"
                                                    : "automatic local") +
              " read before assignment: " +
              std::string(symbol.name));
      });
  EvalContext context(*scope);
  for(const auto &[formal, value] : function_constants)
    context.createLocal(formal, value);
  for(const auto &[index, value] : expanded_indices)
    context.createLocal(&index->as<VariableSymbol>(), value);
  for(const auto &[variable, local] : procedural_locals)
    if(local.constant)
      context.createLocal(&variable->as<VariableSymbol>(), local.constant);
  return e.eval(context);
}

ConstantRange compilert::packed_range(
  const RangeSelectExpression &rs,
  const PackedArrayType &array) const
{
  const auto at = rs.sourceRange.start();
  auto bound = [&](const Expression &operand)
  {
    const auto selected = eval_known(operand);
    if(!selected.isInteger() || selected.integer().hasUnknown())
      fail(
        operand.sourceRange.start(),
        "packed range select requires known constant bounds");
    return selected.integer().as<int32_t>();
  };
  const auto left = bound(rs.left());
  const auto right = bound(rs.right());
  const auto selection_kind = rs.getSelectionKind();
  std::optional<ConstantRange> selection;
  if(left && right)
  {
    if(selection_kind == RangeSelectionKind::Simple)
      selection = ConstantRange{*left, *right};
    else if(*right > 0)
      selection = ConstantRange::getIndexedRange(
        *left,
        *right,
        array.range.isDescending(),
        selection_kind == RangeSelectionKind::IndexedUp);
  }
  if(!selection || !array.range.contains(*selection))
    fail(at, "packed range select is out of range");
  if(
    selection->left != selection->right &&
    selection->isDescending() != array.range.isDescending())
    fail(at, "packed range select goes against the declared direction");
  return *selection;
}

std::optional<exprt>
compilert::dynamic_packed_bank_write(const AssignmentExpression &assignment)
{
  if(assignment.isNonBlocking() || assignment.op || assignment.timingControl)
    return {};
  const auto *range = assignment.left().as_if<RangeSelectExpression>();
  const auto *bank = range ? range->value().as_if<ElementSelectExpression>()
                           : nullptr;
  if(!bank || bank->value().kind != ExpressionKind::NamedValue)
    return {};
  const auto *outer =
    bank->value().type->getCanonicalType().as_if<PackedArrayType>();
  const auto *inner =
    bank->type->getCanonicalType().as_if<PackedArrayType>();
  if(!outer || !inner)
    return {};

  const auto &selector = bank->selector();
  const auto index_at = selector.sourceRange.start();
  require_read_only(selector, "unsupported write in a packed bank index");
  if(eval_known(selector))
    return {};
  if(!covers_packed_dimension(*selector.type, outer->range))
    fail(
      index_at,
      "runtime packed bank write requires an index whose value domain "
      "matches the declared range");

  require_read_only(range->left(), "unsupported write in a packed bank bound");
  require_read_only(range->right(), "unsupported write in a packed bank bound");
  const auto [inner_offset, width] =
    packed_slice_layout(*inner, packed_range(*range, *inner));
  const auto offset_type = full_packed_offset_type(*outer, *selector.type);
  auto offset = packed_element_offset(
    *outer, typecast_exprt::conditional_cast(expr(selector), offset_type));
  auto start = plus_exprt(
    std::move(offset), from_integer(inner_offset, offset_type));

  // Feed a single flat slice to the ordinary blocking partial-write lowering.
  // It reads both the index and the RHS in the current procedural state.
  const auto at = range->sourceRange.start();
  auto lhs = verilog_indexed_part_select_plus_or_minus_exprt(
    ID_verilog_indexed_part_select_plus,
    expr(bank->value()),
    std::move(start),
    index_constant(width),
    type(*range->type, at));
  lhs.add_source_location() = loc(at);
  auto write = item(
    ID_verilog_blocking_assign, {std::move(lhs), expr(assignment.right())});
  return write.with_source_location(loc(assignment.sourceRange.start()));
}

std::optional<exprt>
compilert::dynamic_packed_write(const AssignmentExpression &assignment)
{
  const auto *element = assignment.left().as_if<ElementSelectExpression>();
  if(
    !element || !assignment.isNonBlocking() || assignment.timingControl ||
    element->value().kind != ExpressionKind::NamedValue)
    return {};
  const auto *array =
    element->value().type->getCanonicalType().as_if<PackedArrayType>();
  if(!array || !has_packed_elements(*array))
    return {};
  const auto &selector = element->selector();
  if(!selector.type->isIntegral() || eval_known(selector).isInteger())
    return {};

  const auto stride = array->elementType.getBitWidth();
  // Keep every original index and declared endpoint representable, including
  // their signed difference and its product with the element width.
  const auto index_width =
    std::max<std::size_t>(std::size_t{selector.type->getBitWidth()} + 1, 33);
  const auto wide_type =
    signedbv_typet(index_width + 1 + std::bit_width(stride));
  const auto index =
    typecast_exprt::conditional_cast(expr(selector), wide_type);
  auto in_range = and_exprt(
    binary_relation_exprt(
      index, ID_ge, from_integer(array->range.lower(), wide_type)),
    binary_relation_exprt(
      index, ID_le, from_integer(array->range.upper(), wide_type)));
  auto lhs = verilog_indexed_part_select_plus_or_minus_exprt(
    ID_verilog_indexed_part_select_plus,
    expr(element->value()),
    packed_element_offset(*array, index),
    index_constant(stride),
    type(*element->type, element->sourceRange.start()));
  lhs.add_source_location() = loc(element->sourceRange.start());
  auto write = item(
    ID_verilog_non_blocking_assign, {std::move(lhs), expr(assignment.right())});
  write.add_source_location() = loc(assignment.sourceRange.start());
  return item(ID_if, {std::move(in_range), std::move(write)});
}

exprt compilert::expr(
  const Expression &e,
  const exprt *compound_target,
  bool lvalue)
{
  const auto at = e.sourceRange.start();
  auto t = type(*e.type, at);
  auto check_select = [&](const Expression &base)
  {
    const auto &c = base.type->getCanonicalType();
    if(c.kind != SymbolKind::PackedArrayType)
      return;
    const auto &a = c.as<PackedArrayType>();
    if(
      !a.range.isDescending() || a.range.lower() != 0 ||
      a.elementType.getBitWidth() != 1)
      fail(
        at,
        "unsupported select of " + base.type->toString() +
          " (expected [N:0] of scalar elements)");
  };
  exprt r;
  switch(e.kind)
  {
  case ExpressionKind::NamedValue:
  case ExpressionKind::HierarchicalValue:
  {
    auto *s = e.getSymbolReference();
    if(!s)
      fail(at, "unbound expression");
    if(auto l = locals.find(s); l != locals.end())
      return l->second;
    if(auto l = expanded_indices.find(s); l != expanded_indices.end())
    {
      r = literal(l->second.integer(), t);
      break;
    }
    if(auto l = procedural_locals.find(s); l != procedural_locals.end())
    {
      if(!l->second.assigned)
        fail(
          at,
          std::string(
            l->second.require_full_assignment ? "combinational local"
                                              : "automatic local") +
            " read before assignment: " + std::string(s->name));
      r = l->second.ref;
      break;
    }
    if(s->kind == SymbolKind::Parameter || s->kind == SymbolKind::EnumValue)
    {
      const auto &v = s->kind == SymbolKind::Parameter
                        ? s->as<ParameterSymbol>().getValue()
                        : s->as<EnumValueSymbol>().getValue();
      if(!v.isInteger())
        fail(at, "nonintegral named constant");
      r = literal(v.integer(), t);
      auto id = s->getHierarchicalPath();
      r.set("#vv_constant", id);
      auto &entry = object.metadata.add("constants").add(id);
      entry.id("constant");
      entry.add(ID_value) = r;
      entry.add(ID_C_source_location) = loc(s->location);
      break;
    }
    if(function)
    {
      if(!is_function_variable(s))
        fail(at, "runtime function reads a nonlocal variable");
      r = symbol_exprt(function_variable_name(*s), t);
    }
    else
      r = symbol_exprt(name(*s), t);
    break;
  }
  case ExpressionKind::LValueReference:
    if(!compound_target)
      fail(at, "unbound compound assignment target");
    r = typecast_exprt::conditional_cast(*compound_target, t);
    break;
  case ExpressionKind::IntegerLiteral:
    r = literal(e.as<IntegerLiteral>().getValue(), t);
    break;
  case ExpressionKind::UnbasedUnsizedIntegerLiteral:
    r = literal(e.as<UnbasedUnsizedIntegerLiteral>().getValue(), t);
    break;
  case ExpressionKind::Conversion:
    r = typecast_exprt::conditional_cast(
      expr(e.as<ConversionExpression>().operand(), compound_target, lvalue), t);
    break;
  case ExpressionKind::UnaryOp:
  {
    auto &u = e.as<UnaryExpression>();
    auto v = expr(u.operand());
    switch(u.op)
    {
    case UnaryOperator::LogicalNot:
      r = not_exprt(boolean(v));
      break;
    case UnaryOperator::BitwiseNot:
      r = node(t.id() == ID_bool ? ID_not : ID_bitnot, t, {v});
      break;
    case UnaryOperator::BitwiseOr:
    case UnaryOperator::BitwiseAnd:
    case UnaryOperator::BitwiseXor:
    {
      // As in the native type checker, a scalar is a one-bit vector.
      if(v.type().id() == ID_bool)
        v = typecast_exprt{std::move(v), unsignedbv_typet{1}};
      const auto id = u.op == UnaryOperator::BitwiseAnd  ? ID_reduction_and
                      : u.op == UnaryOperator::BitwiseOr ? ID_reduction_or
                                                         : ID_reduction_xor;
      r = node(id, t, {std::move(v)});
      break;
    }
    case UnaryOperator::Minus:
      r = node(ID_unary_minus, t, {v});
      break;
    default:
      fail(at, "unsupported unary " + std::string(toString(u.op)));
    }
    break;
  }
  case ExpressionKind::BinaryOp:
  {
    auto &b = e.as<BinaryExpression>();
    using O = BinaryOperator;
    if(
      (b.op == O::Equality || b.op == O::Inequality) &&
      (b.left().type->isString() || b.right().type->isString()))
    {
      EvalContext context(*scope);
      auto value = b.eval(context);
      if(!value.isInteger())
        fail(at, "unsupported runtime string comparison");
      r = literal(value.integer(), t);
      break;
    }
    auto l = expr(b.left(), compound_target);
    auto v = expr(b.right(), compound_target);
    static const std::map<O, irep_idt> op = {
      {O::Equality, ID_equal},
      {O::Inequality, ID_notequal},
      {O::LogicalAnd, ID_and},
      {O::LogicalOr, ID_or},
      {O::LessThan, ID_lt},
      {O::GreaterThan, ID_gt},
      {O::LessThanEqual, ID_le},
      {O::GreaterThanEqual, ID_ge},
      {O::BinaryAnd, ID_bitand},
      {O::BinaryOr, ID_bitor},
      {O::BinaryXor, ID_bitxor},
      {O::Add, ID_plus},
      {O::Subtract, ID_minus},
      {O::Multiply, ID_mult},
      {O::Divide, ID_div},
      {O::Mod, ID_mod},
      {O::ArithmeticShiftLeft, ID_shl},
      {O::LogicalShiftLeft, ID_shl},
      {O::LogicalShiftRight, ID_lshr},
      {O::ArithmeticShiftRight, ID_ashr}};
    auto k = op.find(b.op);
    if(k == op.end())
      fail(at, "unsupported binary " + std::string(toString(b.op)));
    auto id = k->second;
    if(id == ID_ashr && !b.left().type->isSigned())
      id = ID_lshr;
    if(id == ID_and || id == ID_or)
    {
      l = boolean(l);
      v = boolean(v);
    }
    if(t.id() == ID_bool)
    {
      if(id == ID_bitand)
        id = ID_and;
      if(id == ID_bitor)
        id = ID_or;
      if(id == ID_bitxor)
        id = ID_xor;
    }
    r = node(id, t, {l, v});
    break;
  }
  case ExpressionKind::ConditionalOp:
  {
    auto &c = e.as<ConditionalExpression>();
    if(c.conditions.size() != 1 || c.conditions[0].pattern)
      fail(at, "unsupported conditional pattern");
    r = if_exprt(
      boolean(expr(*c.conditions[0].expr)), expr(c.left()), expr(c.right()));
    break;
  }
  case ExpressionKind::Concatenation:
  {
    const bool packed = e.type->isIntegral();
    exprt::operandst operands;
    for(const auto *arg : e.as<ConcatenationExpression>().operands())
    {
      if(packed && arg->type->isVoid())
      {
        // Slang uses void for a zero-width replication operand.
        const auto *rep = arg->as_if<ReplicationExpression>();
        const auto *count = rep ? rep->count().getConstant() : nullptr;
        if(count && count->isInteger() && count->integer().as<int32_t>() == 0)
          continue;
      }
      operands.push_back(expr(*arg, nullptr, lvalue));
    }
    if(packed && operands.empty())
      fail(at, "unsupported empty concatenation");
    if(operands.size() == 1 && t.id() == ID_bool)
      r = typecast_exprt::conditional_cast(std::move(operands.front()), t);
    else
      r = node(ID_concatenation, t, std::move(operands));
    break;
  }
  case ExpressionKind::RangeSelect:
  {
    const auto &rs = e.as<RangeSelectExpression>();
    if(const auto *array =
         rs.value().type->getCanonicalType().as_if<PackedArrayType>();
       array && has_packed_elements(*array))
    {
      const auto selection = packed_range(rs, *array);
      r = packed_slice(expr(rs.value(), nullptr, lvalue), *array, selection, t);
      break;
    }
    check_select(rs.value());
    EvalContext ctx(*scope);
    auto constant_bound = [&](const Expression &bound)
    {
      auto v = bound.eval(ctx);
      if(!v.isInteger() || v.integer().hasUnknown())
        fail(bound.sourceRange.start(), "nonconstant part-select bound");
      return literal(v.integer(), type(*bound.type));
    };
    if(rs.getSelectionKind() == RangeSelectionKind::Simple)
      r = slice(
        expr(rs.value(), nullptr, lvalue),
        constant_bound(rs.left()),
        constant_bound(rs.right()),
        t);
    else
    {
      auto direction = rs.getSelectionKind() == RangeSelectionKind::IndexedUp
                         ? ID_verilog_indexed_part_select_plus
                         : ID_verilog_indexed_part_select_minus;
      // An indexed read produces a vector even at width one. The native
      // lowering uses a bit-vector extract and a same-width padding value.
      const auto slice_type =
        !lvalue && t.id() == ID_bool ? typet(unsignedbv_typet{1}) : t;
      r = verilog_indexed_part_select_plus_or_minus_exprt(
        direction,
        expr(rs.value(), nullptr, lvalue),
        expr(rs.left()),
        constant_bound(rs.right()),
        slice_type);
      if(!lvalue)
        r = typecast_exprt::conditional_cast(std::move(r), t);
    }
    break;
  }
  case ExpressionKind::ElementSelect:
  {
    const auto &el = e.as<ElementSelectExpression>();
    const auto *array =
      el.value().type->getCanonicalType().as_if<PackedArrayType>();
    if(!array || !needs_packed_element_layout(*array))
    {
      r = verilog_bit_select_exprt(
        expr(el.value(), nullptr, lvalue), expr(el.selector()), t);
      break;
    }

    const auto &selector = el.selector();
    const auto index_at = selector.sourceRange.start();
    const auto selected = eval_known(selector);
    if(!selected.isInteger() || selected.integer().hasUnknown())
    {
      if(
        lvalue || selected ||
        (!covers_packed_dimension(*selector.type, array->range) &&
         !modulo_covers_packed_dimension(selector, array->range, *scope)))
        fail(
          index_at,
          "packed element select requires a known constant index or a "
          "runtime read index whose value domain matches the declared range");
      // Evaluate the selector at its original type before converting its
      // proven in-range result for the packed layout calculation.
      auto offset = packed_element_offset(
        *array,
        typecast_exprt::conditional_cast(
          expr(selector), full_packed_offset_type(*array, *selector.type)));
      auto base = expr(el.value());
      if(t.id() == ID_bool)
        r = verilog_bit_select_exprt(std::move(base), std::move(offset), t);
      else
        r = verilog_indexed_part_select_plus_or_minus_exprt(
          ID_verilog_indexed_part_select_plus,
          std::move(base),
          std::move(offset),
          index_constant(array->elementType.getBitWidth()),
          t);
      break;
    }
    const auto index = selected.integer().as<int32_t>();
    if(!index || !array->range.containsPoint(*index))
      fail(index_at, "packed element select index is out of range");
    r = packed_slice(
      expr(el.value(), nullptr, lvalue), *array, {*index, *index}, t);
    break;
  }
  case ExpressionKind::SimpleAssignmentPattern:
  case ExpressionKind::StructuredAssignmentPattern:
  {
    const auto *simple = e.as_if<SimpleAssignmentPatternExpression>();
    const auto elements =
      simple ? simple->elements()
             : e.as<StructuredAssignmentPatternExpression>().elements();
    if(
      const auto *packed = e.type->getCanonicalType().as_if<PackedStructType>())
    {
      if((simple && simple->isLValue) || elements.empty())
        fail(at, "unsupported assignment pattern");
      // Slang resolves setters in member order and recursively expands defaults.
      exprt::operandst operands;
      auto element = elements.begin();
      for(const auto &field : packed->membersOfType<FieldSymbol>())
      {
        if(element == elements.end())
          fail(at, "unsupported assignment pattern");
        operands.push_back(typecast_exprt::conditional_cast(
          expr(**element, nullptr, lvalue),
          type(field.getType(), field.location)));
        ++element;
      }
      if(element != elements.end())
        fail(at, "unsupported assignment pattern");
      if(operands.size() == 1)
        r = typecast_exprt::conditional_cast(std::move(operands.front()), t);
      else
        r = node(ID_concatenation, t, std::move(operands));
      break;
    }
    if(
      simple || t.id() != ID_struct ||
      elements.size() != to_struct_type(t).components().size())
      fail(at, "unsupported assignment pattern");
    r = node(ID_struct, t);
    for(auto *field : elements)
      r.operands().push_back(expr(*field, nullptr, lvalue));
    break;
  }
  case ExpressionKind::Replication:
  {
    auto &rep = e.as<ReplicationExpression>();
    auto n = rep.count().getConstant();
    if(!n || !n->isInteger())
      fail(at, "nonconstant replication");
    auto count = n->integer().as<unsigned>();
    if(!count || *count > 65536 || !*count)
      fail(at, "unsupported replication count");
    auto pattern = expr(rep.concat(), nullptr, lvalue);
    if(*count == 1 && t.id() == ID_bool)
      r = typecast_exprt::conditional_cast(std::move(pattern), t);
    else
    {
      r = node(ID_concatenation, t);
      r.operands().assign(*count, pattern);
    }
    break;
  }
  case ExpressionKind::MemberAccess:
  {
    auto &m = e.as<MemberAccessExpression>();
    if(
      m.member.kind == SymbolKind::Field &&
      m.value().type->getCanonicalType().kind == SymbolKind::UnpackedStructType)
    {
      r = member_exprt(
        expr(m.value(), nullptr, lvalue), std::string(m.member.name), t);
      break;
    }
    if(m.member.kind != SymbolKind::Field || !m.value().type->isIntegral())
      fail(at, "unsupported member");
    auto off = m.member.as<FieldSymbol>().bitOffset;
    r = slice(
      expr(m.value(), nullptr, lvalue),
      index_constant(off + e.type->getBitWidth() - 1),
      index_constant(off),
      t);
    break;
  }
  case ExpressionKind::Call:
  {
    auto &c = e.as<CallExpression>();
    if(
      function && c.isSystemCall() &&
      (c.getSubroutineName() == "$past" || c.getSubroutineName() == "$stable"))
      fail(at, "sampled function inside a runtime function body");
    if(c.isSystemCall() && c.getSubroutineName() == "$past")
    {
      const auto args = c.arguments();
      if(args.size() > 2)
      {
        const auto &unsupported =
          args.size() == 4 && args[2]->kind == ExpressionKind::EmptyArgument
            ? *args[3]
            : *args[2];
        fail(
          unsupported.sourceRange.start(),
          "unsupported gate or clock argument to $past");
      }

      if(args.empty())
        fail(at, "$past requires a sampled expression");

      auto ticks = index_constant(1);
      if(args.size() == 2)
      {
        const auto &depth = *args[1];
        const auto value = eval_known(depth);
        const auto n =
          value.isInteger() ? value.integer().as<int32_t>() : std::nullopt;
        if(!n || *n <= 0)
          fail(
            depth.sourceRange.start(),
            "$past depth must be a static positive integer");
        ticks = index_constant(*n);
      }
      r = verilog_past_exprt(expr(*args.front()), std::move(ticks));
    }
    else if(
      c.isSystemCall() && c.arguments().size() == 1 &&
      (c.getSubroutineName() == "$signed" ||
       c.getSubroutineName() == "$unsigned"))
      r = typecast_exprt::conditional_cast(expr(*c.arguments().front()), t);
    else if(!c.isSystemCall())
    {
      const auto *callee = std::get<const SubroutineSymbol *>(c.subroutine);
      if(active_functions.count(callee))
        fail(at, "recursive runtime function");
      EvalContext context(*scope);
      const auto value = c.eval(context);
      r = value.isInteger() ? literal(value.integer(), t) : runtime_call(c);
    }
    else if(
      c.getSubroutineName() == "$clog2" || c.getSubroutineName() == "$bits")
    {
      EvalContext context(*scope);
      auto value = c.eval(context);
      if(!value.isInteger())
        fail(
          at,
          "requires an integral constant call to " +
            std::string(c.getSubroutineName()));
      r = literal(value.integer(), t);
    }
    else if(c.isSystemCall() && c.getSubroutineName() == "$stable")
    {
      if(!in_clocked_property || c.arguments().size() != 1)
        fail(at, "$stable requires the enclosing property's single posedge");
      r = node(
        ID_function_call,
        bool_typet(),
        {verilog_identifier_exprt("$stable"),
         item(ID_arguments, {expr(*c.arguments().front())})});
    }
    else
      fail(at, "unsupported function " + std::string(c.getSubroutineName()));
    break;
  }
  default:
    fail(at, "unsupported expression " + std::string(toString(e.kind)));
  }
  r.add_source_location() = loc(at);
  return r;
}
std::string compilert::clock(const TimingControl *tc, SourceLocation at)
{
  if(!tc || tc->kind != TimingControlKind::SignalEvent)
    fail(at, "requires one resolved signal event");
  auto &ev = tc->as<SignalEventControl>();
  auto *sym = ev.expr.getSymbolReference();
  if(
    ev.edge != EdgeKind::PosEdge ||
    ev.expr.kind != ExpressionKind::NamedValue || !sym || ev.iffCondition ||
    !ev.expr.type->isIntegral() || ev.expr.type->getBitWidth() != 1)
    fail(at, "expected an unguarded single posedge");
  auto id = name(*sym);
  std::set<std::string> visited;
  while(clock_alias.count(id))
  {
    if(!visited.insert(id).second)
      fail(at, "cycle in clock connections");
    id = clock_alias.at(id);
  }
  auto &meta = object.metadata;
  if(!meta.get("clock").empty() && id2string(meta.get("clock")) != id)
    fail(at, "multiple clock domains");
  meta.set("clock", id);
  return id;
}
exprt compilert::assertion(
  const AssertionExpr &a,
  SourceLocation at,
  irept &references)
{
  if(a.syntax)
    at = a.syntax->sourceRange().start();
  exprt r;
  switch(a.kind)
  {
  case AssertionExprKind::Clocking:
  {
    auto &c = a.as<ClockingAssertionExpr>();
    clock(&c.clocking, at);
    r = assertion(c.expr, at, references);
    break;
  }
  case AssertionExprKind::Simple:
  {
    auto &s = a.as<SimpleAssertionExpr>();
    if(s.repetition)
    {
      const auto &repetition = *s.repetition;
      if(
        repetition.kind != SequenceRepetition::Consecutive ||
        s.expr.kind == ExpressionKind::AssertionInstance)
        fail(at, "unsupported sequence repetition");
      if(repetition.range.max)
      {
        // Slang normalizes [*N] and [*N:N] to the same range.
        const auto *source_syntax = a.syntax;
        while(source_syntax && source_syntax->kind ==
                                 syntax::SyntaxKind::ParenthesizedSequenceExpr)
          source_syntax =
            source_syntax->as<syntax::ParenthesizedSequenceExprSyntax>().expr;
        const auto *simple =
          source_syntax
            ? source_syntax->as_if<syntax::SimpleSequenceExprSyntax>()
            : nullptr;
        if(
          !simple || !simple->repetition || !simple->repetition->selector ||
          simple->repetition->selector->kind != syntax::SyntaxKind::BitSelect)
          fail(at, "unsupported sequence repetition");
        r = sva_sequence_repetition_star_exprt{
          as_sequence(boolean(expr(s.expr))),
          from_integer(repetition.range.min, integer_typet{})};
      }
      else
      {
        if(repetition.range.min != 0)
          fail(at, "unsupported sequence repetition");
        r = sva_sequence_repetition_star_exprt{
          as_sequence(boolean(expr(s.expr))),
          from_integer(0, integer_typet{}),
          infinity_exprt{natural_typet{}}};
      }
      break;
    }
    if(const auto *instance = s.expr.as_if<AssertionInstanceExpression>())
    {
      const auto reference_at = instance->sourceRange.start();
      const auto *definition = instance->symbol.as_if<PropertySymbol>();
      if(!definition)
        fail(reference_at, "unsupported named assertion item");
      if(!definition->ports.empty() || !instance->arguments.empty())
        fail(reference_at, "named property with formal arguments");
      if(instance->isRecursiveProperty)
        fail(reference_at, "unsupported recursive named property");
      if(!instance->localVars.empty())
        fail(reference_at, "named property with assertion-local variables");

      irept reference("property_reference");
      reference.set(ID_identifier, name(*definition));
      reference.add(ID_C_source_location) = loc(reference_at);
      reference.add("definition_location") = loc(definition->location);
      references.get_sub().push_back(std::move(reference));

      const auto *enclosing_scope = scope;
      scope = definition;
      auto body = assertion(instance->body, definition->location, references);
      scope = enclosing_scope;
      return body;
    }
    r = boolean(expr(s.expr));
    break;
  }
  case AssertionExprKind::DisableIff:
  {
    auto &d = a.as<DisableIffAssertionExpr>();
    r = node(
      ID_sva_disable_iff,
      verilog_sva_property_typet(),
      {boolean(expr(d.condition)),
       as_property(assertion(d.expr, at, references))});
    break;
  }
  case AssertionExprKind::Binary:
  {
    auto &b = a.as<BinaryAssertionExpr>();
    auto k = b.op;
    if(k == BinaryAssertionOperator::Throughout)
    {
      r = node(
        ID_sva_sequence_throughout,
        verilog_sva_sequence_typet(),
        {assertion(b.left, at, references),
         as_sequence(assertion(b.right, at, references))});
      break;
    }
    if(
      k != BinaryAssertionOperator::OverlappedImplication &&
      k != BinaryAssertionOperator::NonOverlappedImplication)
      fail(at, "unsupported assertion operator " + std::string(toString(k)));
    r = node(
      k == BinaryAssertionOperator::OverlappedImplication
        ? ID_sva_overlapped_implication
        : ID_sva_non_overlapped_implication,
      verilog_sva_property_typet(),
      {as_sequence(assertion(b.left, at, references)),
       as_property(assertion(b.right, at, references))});
    break;
  }
  case AssertionExprKind::SequenceConcat:
  {
    const auto &c = a.as<SequenceConcatExpr>();
    r = nil_exprt();
    bool lhs_admits_empty = false;
    for(const auto &part : c.elements)
    {
      if(!part.delay.max)
        fail(at, "unbounded sequence");
      auto atom = as_sequence(assertion(*part.sequence, at, references));
      const auto fixed_delay = part.delay.min == *part.delay.max;
      const auto rhs_admits_empty =
        part.sequence->checkNondegeneracy().status.has(
          NondegeneracyStatus::AdmitsEmpty);
      if(r.is_nil() && fixed_delay && part.delay.min == 0)
      {
        lhs_admits_empty = rhs_admits_empty;
        r = atom;
      }
      else
      {
        // The sequence rewriter handles empty matches at a fixed delay.
        if(!fixed_delay && (lhs_admits_empty || rhs_admits_empty))
          fail(
            part.delayRange.start() ? part.delayRange.start() : at,
            "ranged sequence delay next to a sequence admitting an empty "
            "match");
        r = node(
          ID_sva_cycle_delay,
          verilog_sva_sequence_typet(),
          {r,
           index_constant(part.delay.min),
           fixed_delay ? nil_exprt{} : index_constant(*part.delay.max),
           atom});
        lhs_admits_empty =
          lhs_admits_empty && rhs_admits_empty && part.delay.min != 0;
      }
    }
    break;
  }
  default:
    fail(at, "unsupported SVA " + std::string(toString(a.kind)));
  }
  r.add_source_location() = loc(at);
  return r;
}
void compilert::property(
  const ConcurrentAssertionStatement &a,
  const Symbol &label,
  exprt &items)
{
  auto at = a.sourceRange.start();
  const auto *timing = clocks.count(&a) ? clocks.at(&a) : nullptr;
  auto timing_at = timing ? timing->sourceRange.start() : SourceLocation{};
  auto actual = clock(timing, timing_at ? timing_at : at);
  if(
    (a.ifTrue && a.ifTrue->kind != StatementKind::Empty) ||
    (a.ifFalse && a.ifFalse->kind != StatementKind::Empty))
    fail(at, "unsupported assertion action");
  auto &s = add(label, bool_typet());
  s.is_property = true;
  auto origin = a.assertionKind;
  irep_idt id;
  if(origin == AssertionKind::Assert)
    id = ID_verilog_assert_property;
  else if(origin == AssertionKind::Assume)
    id = ID_verilog_assume_property;
  else if(origin == AssertionKind::CoverProperty)
    id = ID_verilog_cover_property;
  else
    fail(at, "unsupported assertion kind");
  irept references("property_references");
  in_clocked_property = true;
  auto property_expr = as_property(assertion(a.propertySpec, at, references));
  in_clocked_property = false;
  auto entry = item(
    id,
    {std::move(property_expr), item(ID_verilog_action_then, {item(ID_skip)})});
  entry.set(ID_identifier, s.name);
  entry.set(ID_base_name, s.base_name);
  entry.add_source_location() = s.location;
  items.operands().push_back(entry);
  auto &info = object.metadata.add("properties").add(s.name);
  info.id("property");
  info.set("clock", actual);
  info.set("sampling", sampling);
  info.set("edge", "posedge");
  info.set("origin", "source");
  info.set("kind", id);
  info.add(ID_C_source_location) = s.location;
  if(!references.get_sub().empty())
    info.add("references") = std::move(references);
}
} // namespace vv
