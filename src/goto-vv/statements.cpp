#include "lower.h"
namespace vv
{
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
    r = stmt(s.as<BlockStatement>().body);
    break;
  case StatementKind::List:
  {
    r = item(ID_block);
    for(auto *part : s.as<StatementList>().list)
      r.operands().push_back(stmt(*part));
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
    r = item(
      ID_if,
      {boolean(expr(*c.conditions[0].expr)),
       stmt(c.ifTrue),
       c.ifFalse ? stmt(*c.ifFalse) : nil_exprt()});
    break;
  }
  case StatementKind::ExpressionStatement:
  {
    auto &e = s.as<ExpressionStatement>().expr;
    if(e.kind != ExpressionKind::Assignment)
      fail(at, "expected assignment");
    auto &a = e.as<AssignmentExpression>();
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
