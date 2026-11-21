#include "lower.h"
namespace vv
{
exprt compilert::immediate(
  const ImmediateAssertionStatement &a,
  const Symbol *label)
{
  auto at = a.sourceRange.start();
  if(a.assertionKind != AssertionKind::Assert || a.isDeferred)
    fail(at, "unsupported procedural assertion kind or timing");
  auto diagnostic_only = [](const Statement *action)
  {
    if(!action || action->kind == StatementKind::Empty)
      return true;
    if(action->kind != StatementKind::ExpressionStatement)
      return false;
    auto &e = action->as<ExpressionStatement>().expr;
    if(e.kind != ExpressionKind::Call)
      return false;
    auto &call = e.as<CallExpression>();
    auto task = call.getSubroutineName();
    if(!call.isSystemCall() || (task != "$error" && task != "$warning"))
      return false;
    return std::all_of(
      call.arguments().begin(),
      call.arguments().end(),
      [](const Expression *arg)
      {
        return arg->kind == ExpressionKind::StringLiteral ||
               arg->kind == ExpressionKind::IntegerLiteral ||
               arg->kind == ExpressionKind::NamedValue;
      });
  };
  if(!diagnostic_only(a.ifTrue) || !diagnostic_only(a.ifFalse))
    fail(at, "unsupported procedural assertion action");
  const auto where = loc(at);
  std::string base = label ? std::string(label->name)
                           : "$assert_" + id2string(where.get_line()) + "_" +
                               id2string(where.get_column());
  auto id = label ? name(*label) : name(*scope) + "." + base;
  symbolt symbol(id, bool_typet(), ID_Verilog);
  symbol.base_name = base;
  symbol.pretty_name = id.substr(std::string(root).size() + 1);
  symbol.module = root;
  symbol.location = where;
  symbol.is_property = true;
  if(object.symbols.add(symbol))
    fail(at, "duplicate procedural assertion " + id);

  auto result = item(
    ID_verilog_immediate_assert,
    {boolean(expr(a.cond)), item(ID_verilog_action_then, {item(ID_skip)})});
  result.set(ID_identifier, id);
  result.set(ID_base_name, base);
  result.add_source_location() = where;
  auto &info = object.metadata.add("properties").add(id);
  info.id("property");
  info.set("sampling", "procedural");
  info.set("origin", "source");
  info.set("kind", ID_verilog_immediate_assert);
  info.add(ID_C_source_location) = where;
  return result;
}

exprt compilert::stmt(const Statement &s)
{
  const auto at = s.sourceRange.start();
  exprt r;
  switch(s.kind)
  {
  case StatementKind::Empty:
    r = item(ID_skip);
    break;
  case StatementKind::Block:
  {
    auto &block = s.as<BlockStatement>();
    if(
      block.blockSymbol && !block.blockSymbol->name.empty() &&
      block.body.kind == StatementKind::ImmediateAssertion)
      return immediate(
        block.body.as<ImmediateAssertionStatement>(), block.blockSymbol);
    r = stmt(block.body);
  }
  break;
  case StatementKind::ImmediateAssertion:
    r = immediate(s.as<ImmediateAssertionStatement>(), nullptr);
    break;
  case StatementKind::List:
  {
    r = item(ID_block);
    auto list = s.as<StatementList>().list;
    for(size_t n = 0; n < list.size(); n++)
    {
      if(list[n]->kind != StatementKind::VariableDeclaration)
      {
        r.operands().push_back(stmt(*list[n]));
        continue;
      }
      auto &v = list[n]->as<VariableDeclStatement>().symbol;
      if(n + 1 >= list.size() || list[n + 1]->kind != StatementKind::ForLoop)
        fail(v.location, "unsupported procedural local outside a counted loop");
      auto &loop = list[++n]->as<ForLoopStatement>();
      auto init = v.getInitializer();
      if(
        !init || !loop.stopExpr || !loop.initializers.empty() ||
        loop.steps.size() != 1 ||
        loop.steps[0]->kind != ExpressionKind::UnaryOp)
        fail(at, "requires a statically bounded counted loop");
      auto &step = loop.steps[0]->as<UnaryExpression>();
      if(
        step.op != UnaryOperator::Postincrement ||
        step.operand().getSymbolReference() != &v)
        fail(at, "expected an increment of the loop-local index");
      EvalContext context(v);
      auto first = init->eval(context);
      if(!first.isInteger())
        fail(v.location, "nonconstant loop initializer");
      context.createLocal(&v, first);
      for(unsigned count = 0;; count++)
      {
        auto condition = loop.stopExpr->eval(context);
        if(!condition.isInteger() || condition.integer().hasUnknown())
          fail(at, "nonconstant loop guard");
        if(!condition.isTrue())
          break;
        if(count >= 65536)
          fail(at, "static loop exceeds the expansion limit");
        locals[&v] =
          literal(context.findLocal(&v)->integer(), type(v.getType()));
        r.operands().push_back(stmt(loop.body));
        auto updated = loop.steps[0]->eval(context);
        if(!updated.isInteger())
          fail(at, "cannot evaluate loop step");
      }
      locals.erase(&v);
    }
    break;
  }
  case StatementKind::Case:
  {
    const auto &c = s.as<CaseStatement>();
    if(
      c.condition != CaseStatementCondition::Normal ||
      c.check != UniquePriorityCheck::None)
      fail(at, "unsupported case matching mode");
    r = item(ID_verilog_case, {expr(c.expr)});
    for(auto &group : c.items)
    {
      auto labels = item(ID_empty_string);
      for(auto *x : group.expressions)
        labels.operands().push_back(expr(*x));
      r.operands().push_back(item(ID_case_item, {labels, stmt(*group.stmt)}));
    }
    if(c.defaultCase)
      r.operands().push_back(
        item(ID_case_item, {item(ID_default), stmt(*c.defaultCase)}));
    break;
  }
  case StatementKind::Timed:
  {
    auto &ts = s.as<TimedStatement>();
    clock(&ts.timing, at);
    auto &ev = ts.timing.as<SignalEventControl>();
    auto edge = node(ID_posedge, bool_typet(), {expr(ev.expr)});
    r = item(
      ID_event_guard,
      {node(ID_verilog_event, bool_typet(), {edge}), stmt(ts.stmt)});
    break;
  }
  case StatementKind::Conditional:
  {
    auto &c = s.as<ConditionalStatement>();
    if(c.conditions.size() != 1 || c.conditions[0].pattern)
      fail(at, "unsupported statement condition");
    r = item(ID_if, {boolean(expr(*c.conditions[0].expr)), stmt(c.ifTrue)});
    if(c.ifFalse)
      r.operands().push_back(stmt(*c.ifFalse));
    break;
  }
  case StatementKind::ExpressionStatement:
  {
    auto &e = s.as<ExpressionStatement>().expr;
    if(e.kind != ExpressionKind::Assignment)
      fail(at, "expected assignment");
    auto &a = e.as<AssignmentExpression>();
    if(locals.count(a.left().getSymbolReference()))
      fail(at, "write to a statically expanded loop index");
    if(a.op)
      fail(at, "unsupported compound assignment");
    r = item(
      a.isNonBlocking() ? ID_verilog_non_blocking_assign
                        : ID_verilog_blocking_assign,
      {expr(a.left()), expr(a.right())});
    break;
  }
  default:
    fail(at, "unsupported statement " + std::string(toString(s.kind)));
  }
  r.add_source_location() = loc(at);
  return r;
}
} // namespace vv
