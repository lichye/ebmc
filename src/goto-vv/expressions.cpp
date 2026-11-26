#include <util/bitvector_expr.h>
#include <verilog/sva_expr.h>

#include <slang/ast/ASTVisitor.h>

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
// For an integral `to`, the integral type `from` with the signedness of `to`.
typet with_signedness(const typet &from, const typet &to)
{
  const bool signed_to =
    to.id() == ID_signedbv || to.id() == ID_verilog_signedbv;
  if(
    !signed_to && to.id() != ID_unsignedbv && to.id() != ID_verilog_unsignedbv)
    return from;
  typet result = from;
  if(from.id() == ID_signedbv || from.id() == ID_unsignedbv)
    result.id(signed_to ? ID_signedbv : ID_unsignedbv);
  else if(
    from.id() == ID_verilog_signedbv || from.id() == ID_verilog_unsignedbv)
    result.id(signed_to ? ID_verilog_signedbv : ID_verilog_unsignedbv);
  return result;
}
exprt slice(exprt base, exprt hi, exprt lo, const typet &t)
{
  return node(
    ID_verilog_non_indexed_part_select,
    t,
    {std::move(base), std::move(hi), std::move(lo)});
}

std::optional<int32_t> known_int32(const ConstantValue &value)
{
  if(!value.isInteger() || value.integer().hasUnknown())
    return {};
  return value.integer().as<int32_t>();
}

std::optional<ConstantRange> indexed_range(
  int32_t start,
  int32_t width,
  ConstantRange declared,
  RangeSelectionKind kind)
{
  if(width <= 0)
    return {};
  return ConstantRange::getIndexedRange(
    start,
    width,
    declared.isDescending(),
    kind == RangeSelectionKind::IndexedUp);
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

/// A signed type that holds every value of a selector of \p selector type
/// and its distance from any declared index of \p array, times the element
/// width, so that the layout arithmetic cannot wrap.
signedbv_typet
wide_packed_offset_type(const PackedArrayType &array, const Type &selector)
{
  const std::size_t index_bits =
    std::max<std::size_t>(selector.getBitWidth(), 32);
  const std::size_t stride_bits =
    std::bit_width(uint64_t(array.elementType.getBitWidth()));
  return signedbv_typet(index_bits + stride_bits + 3);
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

bool hierarchical_procedural_local(
  const Expression &reference,
  const Symbol &symbol)
{
  const auto *parent = symbol.getParentScope();
  return reference.kind == ExpressionKind::HierarchicalValue &&
         symbol.kind == SymbolKind::Variable && parent &&
         parent->asSymbol().kind == SymbolKind::StatementBlock;
}
} // namespace

ConstantValue compilert::eval_known(const Expression &e) const
{
  bool needs_slice_read_check = false;
  if(!procedural_locals.empty())
    e.visitSymbolReferences(
      [&](const Expression &reference, const Symbol &symbol)
      {
        if(
          in_always_comb_procedure &&
          hierarchical_procedural_local(reference, symbol))
          fail(
            reference.sourceRange.start(),
            "hierarchical reference to a procedural local outside its "
            "lexical scope");
        const auto local = procedural_locals.find(&symbol);
        if(local == procedural_locals.end() || local->second.assigned)
          return;
        if(!local->second.assigned_bits.empty())
        {
          // The reference alone does not identify which bits are read.
          // Defer validation to expression lowering.
          needs_slice_read_check = true;
          return;
        }
        fail(
          reference.sourceRange.start(),
          std::string(
            local->second.require_full_assignment ? "combinational local"
                                                  : "automatic local") +
            " read before assignment: " +
            std::string(symbol.name));
      });
  if(needs_slice_read_check || has_checked_call(e))
    return {};
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
    else
      selection = indexed_range(*left, *right, array.range, selection_kind);
  }
  if(!selection || !array.range.contains(*selection))
    fail(at, "packed range select is out of range");
  if(
    selection->left != selection->right &&
    selection->isDescending() != array.range.isDescending())
    fail(at, "packed range select goes against the declared direction");
  return *selection;
}

std::optional<compilert::local_slicet>
compilert::static_local_slice(const Expression &e) const
{
  if(!in_always_comb_procedure || procedural_locals.empty())
    return {};
  const auto *bit = e.as_if<ElementSelectExpression>();
  const auto *range = e.as_if<RangeSelectExpression>();
  if(!bit && !range)
    return {};
  const auto &base = bit ? bit->value() : range->value();
  const auto *symbol = base.getSymbolReference();
  const auto local = procedural_locals.find(symbol);
  // Use the combinational-local lifecycle. Expanded-loop locals retain their
  // existing initializer and whole-write rules.
  if(
    base.kind != ExpressionKind::NamedValue || local == procedural_locals.end() ||
    !local->second.require_full_assignment)
    return {};
  const auto *array = base.type->getCanonicalType().as_if<PackedArrayType>();
  if(!array || !array->elementType.isScalar() ||
     symbol->as<VariableSymbol>().lifetime != VariableLifetime::Automatic)
    return {};

  const auto &index = bit ? bit->selector() : range->left();
  require_read_only(index, "unsupported write in a packed local slice index");
  const auto start = known_int32(eval_known(index));
  if(!start)
    return {};
  std::optional<ConstantRange> selection = ConstantRange{*start, *start};
  if(range)
  {
    require_read_only(
      range->right(), "unsupported write in a packed local slice bound");
    const auto bound = known_int32(eval_known(range->right()));
    if(!bound)
      return {};
    const auto kind = range->getSelectionKind();
    selection = kind == RangeSelectionKind::Simple
                  ? std::optional{ConstantRange{*start, *bound}}
                  : indexed_range(*start, *bound, array->range, kind);
  }
  if(
    !selection || !array->range.contains(*selection) ||
    (selection->left != selection->right &&
     selection->isDescending() != array->range.isDescending()))
    return {};

  const auto [offset, width] = packed_slice_layout(*array, *selection);
  if(width != e.type->getBitWidth())
    return {};
  return local_slicet{symbol, offset, width};
}

exprt compilert::local_slice_expr(
  const local_slicet &selection,
  const typet &type,
  SourceLocation at,
  bool lvalue) const
{
  const auto &local = procedural_locals.at(selection.symbol);
  if(!lvalue && !local.assigned)
  {
    const auto &bits = local.assigned_bits;
    if(
      bits.empty() ||
      !std::all_of(
        bits.begin() + selection.offset,
        bits.begin() + selection.offset + selection.width,
        [](bool assigned) { return assigned; }))
      fail(
        at,
        "combinational local slice read before assignment: " +
          std::string(selection.symbol->name));
  }
  auto result = local.ref.type().id() == ID_bool && type.id() == ID_bool
                  ? exprt{local.ref}
                  : slice(
                      local.ref,
                      index_constant(selection.offset + selection.width - 1),
                      index_constant(selection.offset),
                      type);
  return result.with_source_location(loc(at));
}

/// A read of a packed array element with a runtime index whose values can
/// fall outside the declared range, e.g. data_i[in][in_p] with a one-bit
/// in_p on a one-element dimension (1800-2017 7.4.6, 11.5.1). The index is
/// compared with the declared bounds at its full value, never narrowed
/// first, so an out-of-range value cannot wrap onto a declared element. It
/// then reads x for a four-state element and 0 for a two-state element.
exprt compilert::runtime_packed_element_read(
  const ElementSelectExpression &el,
  const PackedArrayType &array,
  const typet &t)
{
  const auto &selector = el.selector();
  const auto wide = wide_packed_offset_type(array, *selector.type);
  // Sign or zero extension keeps the selector value.
  auto index = typecast_exprt::conditional_cast(expr(selector), wide);
  auto in_range = and_exprt{
    binary_relation_exprt{
      index, ID_ge, from_integer(array.range.lower(), wide)},
    binary_relation_exprt{
      index, ID_le, from_integer(array.range.upper(), wide)}};
  auto offset = packed_element_offset(array, std::move(index));
  auto base = expr(el.value());
  const auto element_width = array.elementType.getBitWidth();
  exprt read;
  if(t.id() == ID_bool)
    read = verilog_bit_select_exprt(std::move(base), std::move(offset), t);
  else
    read = verilog_indexed_part_select_plus_or_minus_exprt(
      ID_verilog_indexed_part_select_plus,
      std::move(base),
      std::move(offset),
      index_constant(element_width),
      t);
  exprt missing = array.elementType.isFourState()
                    ? typecast_exprt::conditional_cast(
                        constant_exprt(
                          std::string(element_width, 'x'),
                          verilog_unsignedbv_typet(element_width)),
                        t)
                    : typecast_exprt::conditional_cast(
                        from_integer(0, unsignedbv_typet(element_width)), t);
  return if_exprt{std::move(in_range), std::move(read), std::move(missing), t};
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
  if(gated_clock(e))
    fail(at, "clock gate contract does not support gated clock data use");
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
    if(
      in_always_comb_procedure && !procedural_locals.empty() &&
      hierarchical_procedural_local(e, *s))
      fail(
        at,
        "hierarchical reference to a procedural local outside its "
        "lexical scope");
    if(auto l = locals.find(s); l != locals.end())
      return l->second;
    if(auto l = expanded_indices.find(s); l != expanded_indices.end())
    {
      r = literal(l->second.integer(), t);
      break;
    }
    if(auto l = procedural_locals.find(s); l != procedural_locals.end())
    {
      // Whole local writes are handled by stmt, which tracks assignment.
      if(lvalue)
        fail(at, "unsupported assignment target containing a procedural local");
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
  {
    auto &conversion = e.as<ConversionExpression>();
    auto v = expr(conversion.operand(), compound_target, lvalue);
    // A context-determined operand takes the signedness of the expression
    // before it is extended (1800-2017 11.8.2; slang's ConversionExpression
    // ::convert). A typecast extends by the signedness of its operand.
    if(conversion.conversionKind == ConversionKind::Propagated && !lvalue)
      v = typecast_exprt::conditional_cast(
        std::move(v), with_signedness(v.type(), t));
    r = typecast_exprt::conditional_cast(std::move(v), t);
    // An explicit cast to a two-state type converts x and z to 0
    // (1800-2017 6.11, 6.24.1). Mark it as the native type checker does, so
    // that a write of the result is not sampled like an explicit x/z.
    if(
      !conversion.isImplicit() && !lvalue && !e.type->isFourState() &&
      conversion.operand().type->isFourState())
    {
      r = verilog_explicit_type_cast_exprt{std::move(r), t};
    }
    break;
  }
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
    case UnaryOperator::BitwiseNor:
    case UnaryOperator::BitwiseAnd:
    case UnaryOperator::BitwiseNand:
    case UnaryOperator::BitwiseXor:
    case UnaryOperator::BitwiseXnor:
    {
      // As in the native type checker, a scalar is a one-bit vector.
      if(v.type().id() == ID_bool)
        v = typecast_exprt{std::move(v), unsignedbv_typet{1}};
      using O = UnaryOperator;
      const auto id = u.op == O::BitwiseAnd || u.op == O::BitwiseNand
                        ? ID_reduction_and
                      : u.op == O::BitwiseOr || u.op == O::BitwiseNor
                        ? ID_reduction_or
                        : ID_reduction_xor;
      r = node(id, t, {std::move(v)});
      if(
        u.op == O::BitwiseNor || u.op == O::BitwiseNand ||
        u.op == O::BitwiseXnor)
        r = not_exprt{std::move(r)};
      break;
    }
    case UnaryOperator::Minus:
      r = typecast_exprt::conditional_cast(
        node(ID_unary_minus, vector_type(t), {as_vector(v)}), t);
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
    if(b.op == O::Power)
    {
      // Slang has already specialized generate constants and propagated the
      // result type to the base. Evaluate without binding procedural locals.
      const char *diagnostic = "unsupported binary Power";
      require_read_only(b, diagnostic);
      reject_checked_call(b);
      EvalContext context(*scope);
      const auto value = b.eval(context);
      if(value.isInteger())
      {
        r = literal(value.integer(), t);
        break;
      }

      const auto width = b.type->getBitWidth();
      const auto &exponent_type = *b.right().type;
      const auto base = b.left().eval(context);
      if(
        !b.left().type->isIntegral() || !exponent_type.isIntegral() ||
        width < 2 || !base.isInteger() || base.integer().as<int>() != 2)
        fail(at, diagnostic);

      // Keep the self-determined exponent intact. Sign extend before
      // interpreting it as an unsigned distance, so a negative exponent
      // is outside [0, width) even when its declared type is very narrow.
      const auto distance_width = std::max(
        exponent_type.getBitWidth(),
        std::bit_width(width) + (exponent_type.isSigned() ? 1u : 0u));
      const unsignedbv_typet distance_type{distance_width};
      auto exponent = expr(b.right());
      if(exponent_type.isSigned())
        exponent = typecast_exprt::conditional_cast(
          std::move(exponent), signedbv_typet{distance_width});
      const auto distance = typecast_exprt::conditional_cast(
        std::move(exponent), distance_type);

      // Compare before a solver can narrow the shift distance to the result
      // width. Keep the full distance in the shift for X/Z propagation.
      r = if_exprt{
        binary_relation_exprt{
          distance, ID_lt, from_integer(width, distance_type)},
        shl_exprt{from_integer(1, t), distance},
        from_integer(0, t)};
      break;
    }
    if(
      (b.op == O::Equality || b.op == O::Inequality) &&
      (b.left().type->isString() || b.right().type->isString()))
    {
      reject_checked_call(b);
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
    if(
      id == ID_plus || id == ID_minus || id == ID_mult || id == ID_div ||
      id == ID_mod || id == ID_shl || id == ID_lshr || id == ID_ashr)
    {
      // Arithmetic and shifts of scalars, including a scalar shift distance,
      // are on one-bit vectors.
      r = typecast_exprt::conditional_cast(
        node(id, vector_type(t), {as_vector(l), as_vector(v)}), t);
      break;
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
    if(auto selection = lvalue ? std::nullopt : static_local_slice(e))
      return local_slice_expr(*selection, t, at, false);
    const auto &rs = e.as<RangeSelectExpression>();
    const auto *array =
      rs.value().type->getCanonicalType().as_if<PackedArrayType>();
    const auto selection_kind = rs.getSelectionKind();
    if(array && has_packed_elements(*array))
    {
      const auto selection = packed_range(rs, *array);
      r = packed_slice(expr(rs.value(), nullptr, lvalue), *array, selection, t);
      break;
    }
    if(lvalue && array && selection_kind != RangeSelectionKind::Simple)
    {
      const auto start = known_int32(eval_known(rs.left()));
      const auto width = known_int32(eval_known(rs.right()));
      const auto selection =
        start && width
          ? indexed_range(*start, *width, array->range, selection_kind)
          : std::nullopt;
      if(
        selection && array->range.contains(*selection) &&
        selection->fullWidth() == e.type->getBitWidth())
      {
        require_read_only(
          rs.left(), "unsupported write in a packed range base");
        require_read_only(
          rs.right(), "unsupported write in a packed range width");
        auto base = expr(rs.value(), nullptr, lvalue);
        // A static in-bounds write is a location, not a guarded read value.
        // The fixed slice uses the packed vector's flattened bit positions.
        if(base.type().id() == ID_bool && t.id() == ID_bool)
          r = std::move(base);
        else
          r = packed_slice(std::move(base), *array, *selection, t);
        break;
      }
    }
    check_select(rs.value());
    EvalContext ctx(*scope);
    auto constant_bound = [&](const Expression &bound)
    {
      reject_checked_call(bound);
      auto v = bound.eval(ctx);
      if(!v.isInteger() || v.integer().hasUnknown())
        fail(bound.sourceRange.start(), "nonconstant part-select bound");
      return literal(v.integer(), type(*bound.type));
    };
    if(selection_kind == RangeSelectionKind::Simple)
      r = slice(
        expr(rs.value(), nullptr, lvalue),
        constant_bound(rs.left()),
        constant_bound(rs.right()),
        t);
    else
    {
      auto direction = selection_kind == RangeSelectionKind::IndexedUp
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
    if(auto selection = lvalue ? std::nullopt : static_local_slice(e))
      return local_slice_expr(*selection, t, at, false);
    const auto &el = e.as<ElementSelectExpression>();
    const auto *array =
      el.value().type->getCanonicalType().as_if<PackedArrayType>();
    if(
      lvalue && array && !has_packed_elements(*array) &&
      array->range.fullWidth() == 1 && t.id() == ID_bool)
    {
      if(known_int32(eval_known(el.selector())) == array->range.right)
      {
        require_read_only(el.selector(), "unsupported write in a packed index");
        auto base = expr(el.value(), nullptr, lvalue);
        if(base.type().id() == ID_bool)
        {
          // Keep the write to the sole declared bit as a whole-object write.
          r = std::move(base);
          break;
        }
      }
    }
    if(!array || !needs_packed_element_layout(*array))
    {
      auto base = expr(el.value(), nullptr, lvalue);
      // Dynamic extraction shifts the source vector by an index of the
      // same bit width. Native typing keeps a one-bit source as a vector.
      if(!lvalue && base.type().id() == ID_bool)
        base = typecast_exprt{std::move(base), unsignedbv_typet{1}};
      r = verilog_bit_select_exprt(
        std::move(base), expr(el.selector()), t);
      break;
    }

    const auto &selector = el.selector();
    const auto index_at = selector.sourceRange.start();
    const auto selected = eval_known(selector);
    if(!selected.isInteger() || selected.integer().hasUnknown())
    {
      if(lvalue || selected)
        fail(
          index_at,
          "packed element select requires a known constant index or a "
          "runtime read index whose value domain matches the declared range");
      if(
        !covers_packed_dimension(*selector.type, array->range) &&
        !modulo_covers_packed_dimension(selector, array->range, *scope))
      {
        r = runtime_packed_element_read(el, *array, t);
        break;
      }
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
    const auto *structured = e.as_if<StructuredAssignmentPatternExpression>();
    const auto elements = simple ? simple->elements() : structured->elements();
    const auto &target = e.type->getCanonicalType();
    const auto lower_element = [&](const Expression &element, const typet &dest)
    {
      // A Slang conversion to a scalar in an assignment pattern truncates
      // to one bit. EBMC's cast directly to Bool tests the entire value.
      if(const auto *conversion = element.as_if<ConversionExpression>();
         dest.id() == ID_bool && conversion && element.type->isIntegral() &&
         element.type->getBitWidth() == 1 &&
         conversion->operand().type->isIntegral() &&
         conversion->operand().type->getBitWidth() > 1)
      {
        auto bit = typecast_exprt::conditional_cast(
          expr(conversion->operand(), nullptr, lvalue), unsignedbv_typet{1});
        return typecast_exprt::conditional_cast(std::move(bit), dest);
      }
      return typecast_exprt::conditional_cast(
        expr(element, nullptr, lvalue), dest);
    };
    if(const auto *array = target.as_if<PackedArrayType>();
       array && array->elementType.isScalar() && structured &&
       structured->defaultSetter && structured->indexSetters.empty() &&
       structured->typeSetters.empty())
    {
      const auto width = array->range.fullWidth();
      if(
        lvalue || elements.empty() || elements.size() != width ||
        !std::all_of(
          elements.begin(),
          elements.end(),
          [&](const auto *element) { return element == elements.front(); }))
        fail(at, "unsupported assignment pattern");

      // Slang binds a default-only vector to one typed scalar per bit. As
      // with explicit replication, each position receives that scalar.
      const auto &element = *elements.front();
      require_read_only(element, "unsupported write in a packed default");
      auto bit = lower_element(element, type(array->elementType, at));
      if(width == 1)
        r = typecast_exprt::conditional_cast(std::move(bit), t);
      else
      {
        r = node(ID_concatenation, t);
        r.operands().assign(width, bit);
      }
      break;
    }
    if(const auto *packed = target.as_if<PackedStructType>())
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
        operands.push_back(
          lower_element(**element, type(field.getType(), field.location)));
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
    const auto subroutine = c.getSubroutineName();
    const bool sampled = subroutine == "$stable" || subroutine == "$rose";
    if(function && c.isSystemCall() && (subroutine == "$past" || sampled))
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
    else if(c.isSystemCall() && subroutine == "$onehot0")
    {
      if(c.arguments().size() != 1 || !c.arguments().front()->type->isIntegral())
        fail(at, "$onehot0 requires one packed integral argument");
      require_read_only(*c.arguments().front(), "unsupported write in $onehot0");
      EvalContext context(*scope);
      const auto value = has_checked_call(c) ? ConstantValue{} : c.eval(context);
      if(value.isInteger())
        r = literal(value.integer(), t);
      else
      {
        auto argument = expr(*c.arguments().front());
        if(argument.type().id() == ID_bool)
          argument = typecast_exprt{std::move(argument), unsignedbv_typet{1}};
        r = onehot0_exprt{std::move(argument)};
      }
    }
    else if(!c.isSystemCall())
    {
      const auto *callee = std::get<const SubroutineSymbol *>(c.subroutine);
      if(active_functions.count(callee))
        fail(at, "recursive runtime function");
      EvalContext context(*scope);
      const auto value = has_checked_call(c) ? ConstantValue{} : c.eval(context);
      r = value.isInteger() ? literal(value.integer(), t) : runtime_call(c);
    }
    else if(
      c.getSubroutineName() == "$clog2" || c.getSubroutineName() == "$bits")
    {
      EvalContext context(*scope);
      const auto value = subroutine == "$clog2" && has_checked_call(c)
                           ? ConstantValue{}
                           : c.eval(context);
      if(!value.isInteger())
        fail(
          at,
          "requires an integral constant call to " +
            std::string(c.getSubroutineName()));
      r = literal(value.integer(), t);
    }
    else if(c.isSystemCall() && sampled)
    {
      if(!in_clocked_property || c.arguments().size() != 1)
        fail(
          at,
          std::string(subroutine) +
            " requires the enclosing property's single posedge");
      const auto &argument = *c.arguments().front();
      if(subroutine == "$rose" && argument.type->isFourState())
      {
        EvalContext context(*scope);
        const auto value = argument.eval(context);
        // Native $past instrumentation cannot initialize an X/Z constant.
        if(value.isInteger() && value.integer().hasUnknown())
          fail(
            argument.sourceRange.start(),
            "unsupported X/Z constant argument to $rose");
      }
      r = node(
        ID_function_call,
        bool_typet(),
        {verilog_identifier_exprt(std::string(subroutine)),
         item(ID_arguments, {expr(argument)})});
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
  const auto *signal = scalar_clock_signal(&ev.expr);
  if(ev.edge != EdgeKind::PosEdge || !signal || ev.iffCondition)
    fail(at, "expected an unguarded single posedge");
  const auto id = name(*clock_source(*signal, at));
  if(clock_gate && !clock_gate_enable(id).empty())
    fail(at, "clock gate contract supports gated process clocks only");
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
exprt compilert::gated_assertion(const AssertionExpr &a, SourceLocation at)
{
  if(a.syntax)
    at = a.syntax->sourceRange().start();
  const auto diagnostic =
    "clock gate contract requires a single-sample assertion";
  if(const auto *binary = a.as_if<BinaryAssertionExpr>();
     binary && binary->op == BinaryAssertionOperator::OverlappedImplication)
    return verilog_implies_exprt{
      gated_assertion(binary->left, at), gated_assertion(binary->right, at)}
      .with_source_location(loc(at));

  const auto *simple = a.as_if<SimpleAssertionExpr>();
  if(
    !simple || simple->repetition ||
    simple->expr.kind == ExpressionKind::AssertionInstance)
    fail(at, diagnostic);

  require_read_only(simple->expr, "unsupported write in a gated assertion");
  simple->expr.visit(makeVisitor(
    [&](auto &visitor, const CallExpression &call)
    {
      const auto task = call.getSubroutineName();
      if(
        call.isSystemCall() &&
        (task == "$past" || task == "$rose" || task == "$fell" ||
         task == "$stable" || task == "$changed" || task == "$sampled"))
        fail(call.sourceRange.start(), diagnostic);
      visitor.visitDefault(call);
    }));
  return boolean(expr(simple->expr)).with_source_location(loc(at));
}

void compilert::property(
  const ConcurrentAssertionStatement &a,
  const Symbol &label,
  exprt &items)
{
  auto at = a.sourceRange.start();
  const auto *timing = clocks.count(&a) ? clocks.at(&a) : nullptr;
  // Analysis can report only the canonical occurrence of an assertion.
  // For an explicit clock, use the bound event from this instance.
  if(!timing)
    if(const auto *explicit_clock = a.propertySpec.as_if<ClockingAssertionExpr>())
      timing = &explicit_clock->clocking;
  const auto timing_at = timing ? timing->sourceRange.start() : SourceLocation{};
  const auto *event = timing ? timing->as_if<SignalEventControl>() : nullptr;
  const auto *source = event ? scalar_clock_signal(&event->expr) : nullptr;
  const auto enable = event ? gated_clock_enable(event->expr) : std::string{};
  auto actual = enable.empty() ? clock(timing, timing_at ? timing_at : at)
                              : clock_gate->clock;
  const AssertionExpr *body = &a.propertySpec;
  if(!enable.empty())
  {
    if(
      event->edge != EdgeKind::PosEdge || event->iffCondition ||
      !event->expr.type->isIntegral() || event->expr.type->getBitWidth() != 1 ||
      a.assertionKind != AssertionKind::Assert)
      fail(timing_at, "clock gate contract requires an unguarded posedge assert");
    if(const auto *explicit_clock = body->as_if<ClockingAssertionExpr>())
    {
      if(!explicit_clock->clocking.isEquivalentTo(*timing))
        fail(timing_at, "clock gate contract requires one assertion clock");
      body = &explicit_clock->expr;
    }
  }

  if(a.ifTrue && a.ifTrue->kind != StatementKind::Empty)
    fail(at, "unsupported assertion action");
  const CallExpression *fatal = nullptr;
  if(a.ifFalse && a.ifFalse->kind != StatementKind::Empty)
  {
    const auto *action = a.ifFalse->as_if<ExpressionStatement>();
    fatal = action ? action->expr.as_if<CallExpression>() : nullptr;
    if(
      enable.empty() || !fatal || !fatal->isSystemCall() ||
      fatal->getSubroutineName() != "$fatal")
      fail(at, "unsupported assertion action");
    for(const auto *argument : fatal->arguments())
      check_diagnostic_argument(*argument);
  }

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
  // The existing gate enable denotes a passed root edge in this preponed
  // sample. The Boolean implication cannot advance to a later root tick.
  auto property_expr =
    enable.empty() ? as_property(assertion(a.propertySpec, at, references))
                   : exprt{verilog_implies_exprt{
                       symbol_exprt{enable, bool_typet()},
                       gated_assertion(*body, at)}};
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
  if(source && name(*source) != actual)
  {
    info.set("source_clock", name(*source));
    info.add("source_clock_location") = loc(timing_at);
  }
  if(!enable.empty())
  {
    info.set("gate_output", name(*clock_source(*source, timing_at)));
    info.set("gate_enable", enable);
    if(fatal)
    {
      info.set("failure_action", "$fatal");
      info.add("failure_action_location") = loc(fatal->sourceRange.start());
    }
  }
  if(!references.get_sub().empty())
    info.add("references") = std::move(references);
}
} // namespace vv
