#include "lower.h"
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
} // namespace
exprt compilert::expr(const Expression &e)
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
    r = symbol_exprt(name(*s), t);
    break;
  }
  case ExpressionKind::IntegerLiteral:
    r = literal(e.as<IntegerLiteral>().getValue(), t);
    break;
  case ExpressionKind::UnbasedUnsizedIntegerLiteral:
    r = literal(e.as<UnbasedUnsizedIntegerLiteral>().getValue(), t);
    break;
  case ExpressionKind::Conversion:
    r = typecast_exprt::conditional_cast(
      expr(e.as<ConversionExpression>().operand()), t);
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
      r = node(ID_reduction_or, t, {v});
      break;
    case UnaryOperator::BitwiseAnd:
      r = node(ID_reduction_and, t, {v});
      break;
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
    auto l = expr(b.left()), v = expr(b.right());
    using O = BinaryOperator;
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
      {O::LogicalShiftLeft, ID_shl},
      {O::LogicalShiftRight, ID_lshr},
      {O::ArithmeticShiftRight, ID_ashr}};
    auto k = op.find(b.op);
    if(k == op.end())
      fail(at, "unsupported binary " + std::string(toString(b.op)));
    auto id = k->second;
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
    r = node(ID_concatenation, t);
    for(auto *a : e.as<ConcatenationExpression>().operands())
      r.operands().push_back(expr(*a));
    break;
  }
  case ExpressionKind::RangeSelect:
  {
    auto &rs = e.as<RangeSelectExpression>();
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
        expr(rs.value()),
        constant_bound(rs.left()),
        constant_bound(rs.right()),
        t);
    else
    {
      auto direction = rs.getSelectionKind() == RangeSelectionKind::IndexedUp
                         ? ID_verilog_indexed_part_select_plus
                         : ID_verilog_indexed_part_select_minus;
      r = verilog_indexed_part_select_plus_or_minus_exprt(
        direction,
        expr(rs.value()),
        expr(rs.left()),
        constant_bound(rs.right()),
        t);
    }
    break;
  }
  case ExpressionKind::ElementSelect:
  {
    auto &el = e.as<ElementSelectExpression>();
    check_select(el.value());
    r = verilog_bit_select_exprt(expr(el.value()), expr(el.selector()), t);
    break;
  }
  case ExpressionKind::StructuredAssignmentPattern:
  {
    auto &pattern = e.as<StructuredAssignmentPatternExpression>();
    if(
      t.id() != ID_struct ||
      pattern.elements().size() != to_struct_type(t).components().size())
      fail(at, "unsupported assignment pattern");
    r = node(ID_struct, t);
    for(auto *field : pattern.elements())
      r.operands().push_back(expr(*field));
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
    auto pattern = expr(rep.concat());
    r = node(ID_concatenation, t);
    r.operands().assign(*count, pattern);
    break;
  }
  case ExpressionKind::MemberAccess:
  {
    auto &m = e.as<MemberAccessExpression>();
    if(
      m.member.kind == SymbolKind::Field &&
      m.value().type->getCanonicalType().kind == SymbolKind::UnpackedStructType)
    {
      r = member_exprt(expr(m.value()), std::string(m.member.name), t);
      break;
    }
    if(m.member.kind != SymbolKind::Field || !m.value().type->isIntegral())
      fail(at, "unsupported member");
    auto off = m.member.as<FieldSymbol>().bitOffset;
    r = slice(
      expr(m.value()),
      index_constant(off + e.type->getBitWidth() - 1),
      index_constant(off),
      t);
    break;
  }
  case ExpressionKind::Call:
  {
    auto &c = e.as<CallExpression>();
    if(c.getSubroutineName() == "$past" && c.arguments().size() == 1)
      r = verilog_past_exprt(expr(*c.arguments().front()), index_constant(1));
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
exprt compilert::assertion(const AssertionExpr &a, SourceLocation at)
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
    r = assertion(c.expr, at);
    break;
  }
  case AssertionExprKind::Simple:
  {
    auto &s = a.as<SimpleAssertionExpr>();
    if(s.repetition)
      fail(at, "unsupported sequence repetition");
    r = boolean(expr(s.expr));
    break;
  }
  case AssertionExprKind::DisableIff:
  {
    auto &d = a.as<DisableIffAssertionExpr>();
    r = node(
      ID_sva_disable_iff,
      verilog_sva_property_typet(),
      {boolean(expr(d.condition)), assertion(d.expr, at)});
    break;
  }
  case AssertionExprKind::Binary:
  {
    auto &b = a.as<BinaryAssertionExpr>();
    auto k = b.op;
    if(
      k != BinaryAssertionOperator::OverlappedImplication &&
      k != BinaryAssertionOperator::NonOverlappedImplication)
      fail(at, "unsupported assertion operator " + std::string(toString(k)));
    auto left = assertion(b.left, at), right = assertion(b.right, at);
    if(left.type().id() == ID_bool)
      left = node(ID_sva_boolean, verilog_sva_sequence_typet(), {left});
    if(right.type().id() == ID_verilog_sva_sequence)
      right =
        node(ID_sva_sequence_property, verilog_sva_property_typet(), {right});
    r = node(
      k == BinaryAssertionOperator::OverlappedImplication
        ? ID_sva_overlapped_implication
        : ID_sva_non_overlapped_implication,
      verilog_sva_property_typet(),
      {left, right});
    break;
  }
  case AssertionExprKind::SequenceConcat:
  {
    const auto &c = a.as<SequenceConcatExpr>();
    r = nil_exprt();
    for(const auto &part : c.elements)
    {
      if(!part.delay.max)
        fail(at, "unbounded sequence");
      auto atom = assertion(*part.sequence, at);
      if(atom.type().id() == ID_bool)
        atom = node(ID_sva_boolean, verilog_sva_sequence_typet(), {atom});
      if(r.is_nil() && part.delay.min == 0 && *part.delay.max == 0)
        r = atom;
      else
        r = node(
          ID_sva_cycle_delay,
          verilog_sva_sequence_typet(),
          {r,
           index_constant(part.delay.min),
           index_constant(*part.delay.max),
           atom});
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
  auto actual = clock(clocks.count(&a) ? clocks.at(&a) : nullptr, at);
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
  auto entry = item(
    id,
    {assertion(a.propertySpec, at),
     item(ID_verilog_action_then, {item(ID_skip)})});
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
}
} // namespace vv
