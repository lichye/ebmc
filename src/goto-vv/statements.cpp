#include <slang/ast/ASTVisitor.h>

#include "lower.h"
namespace vv
{
namespace
{
bool static_compound_target(const Expression &e)
{
  if(const auto *field = e.as_if<MemberAccessExpression>())
    return field->member.kind == SymbolKind::Field &&
           field->value().type->getCanonicalType().kind ==
             SymbolKind::PackedStructType &&
           static_compound_target(field->value());

  if(e.kind != ExpressionKind::NamedValue)
    return false;
  const auto *symbol = e.getSymbolReference();
  return symbol && VariableSymbol::isKind(symbol->kind) &&
         symbol->as<VariableSymbol>().lifetime == VariableLifetime::Static;
}

class automatic_local_visitort
  : public ASTVisitor<automatic_local_visitort, VisitFlags::Statements>
{
public:
  std::set<const VariableSymbol *> declared;
  std::set<const VariableSymbol *> indices;

  void handle(const VariableDeclStatement &declaration)
  {
    if(declaration.symbol.lifetime == VariableLifetime::Automatic)
      declared.insert(&declaration.symbol);
  }

  void handle(const ForLoopStatement &loop)
  {
    indices.insert(loop.loopVars.begin(), loop.loopVars.end());
    visitDefault(loop);
  }
};

bool needs_expanded_loop(const Statement &body)
{
  automatic_local_visitort visitor;
  body.visit(visitor);
  return std::any_of(
    visitor.declared.begin(),
    visitor.declared.end(),
    [&](const auto *declared) { return !visitor.indices.count(declared); });
}
} // namespace

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

// Retain both native event operands for the clocked RTL builder. Only the
// clock is a property sampling domain. The second edge must be the reset
// tested by the process's outermost conditional.
exprt compilert::clock_reset_block(const TimedStatement &block)
{
  const auto at = block.timing.sourceRange.start();
  auto events = block.timing.as<EventListControl>().events;
  if(events.size() != 2)
    fail(at, "expected a clock and one active-high reset edge");
  clock(events[0], at);
  auto *reset = events[1]->as_if<SignalEventControl>();
  const Statement *body = &block.stmt;
  while(auto *compound = body->as_if<BlockStatement>())
    body = &compound->body;
  auto *cond = body->as_if<ConditionalStatement>();
  if(!reset || reset->edge != EdgeKind::PosEdge || reset->iffCondition ||
     reset->expr.kind != ExpressionKind::NamedValue ||
     !reset->expr.type->isIntegral() || reset->expr.type->getBitWidth() != 1 ||
     !cond || cond->conditions.size() != 1 || cond->conditions[0].pattern ||
     cond->conditions[0].expr->kind != ExpressionKind::NamedValue ||
     cond->conditions[0].expr->getSymbolReference() != reset->expr.getSymbolReference())
    fail(at, "expected a posedge reset tested by the outer if");
  auto &clk = events[0]->as<SignalEventControl>();
  auto event = node(ID_verilog_event, bool_typet(), {
    node(ID_posedge, bool_typet(), {expr(clk.expr)}),
    node(ID_posedge, bool_typet(), {expr(reset->expr)})});
  auto guard = item(ID_event_guard, {std::move(event), stmt(block.stmt)});
  guard.add_source_location() = loc(at);
  return guard;
}

exprt compilert::counted_loop(
  const VariableSymbol &v,
  const ForLoopStatement &loop)
{
  const auto at = loop.sourceRange.start();
  const auto *init = v.getInitializer();
  if(
    !init || !loop.stopExpr || !loop.initializers.empty() ||
    loop.steps.size() != 1 || loop.steps[0]->kind != ExpressionKind::UnaryOp)
    fail(at, "requires a statically bounded counted loop");
  const auto &step = loop.steps[0]->as<UnaryExpression>();
  if(
    step.op != UnaryOperator::Postincrement ||
    step.operand().getSymbolReference() != &v)
    fail(at, "expected an increment of the loop-local index");

  EvalContext context(v);
  for(const auto &[index, value] : expanded_indices)
    context.createLocal(&index->as<VariableSymbol>(), value);
  const auto first = init->eval(context);
  if(!first.isInteger() || first.integer().hasUnknown())
    fail(v.location, "nonconstant loop initializer");
  context.createLocal(&v, first);
  const bool expand =
    !expanded_indices.empty() || needs_expanded_loop(loop.body);
  auto expanded = item(ID_block);
  for(unsigned count = 0;; ++count)
  {
    const auto condition = loop.stopExpr->eval(context);
    if(!condition.isInteger() || condition.integer().hasUnknown())
      fail(at, "nonconstant loop guard");
    if(!condition.isTrue())
      break;
    if(count >= 65536)
      fail(at, "static loop exceeds the expansion limit");
    if(expand)
    {
      // Lower this iteration with its actual typed induction value. A packed
      // index must resolve to an in-range value in every emitted iteration.
      expanded_indices.emplace(&v, *context.findLocal(&v));
      expanded.operands().push_back(stmt(loop.body));
      expanded_indices.erase(&v);
    }
    const auto updated = loop.steps[0]->eval(context);
    if(!updated.isInteger())
      fail(at, "cannot evaluate loop step");
  }
  if(expand)
    return expanded.with_source_location(loc(at));

  // Let the RTL builder unroll the other checked loops. Its loop frame
  // merges data-dependent break paths.
  const auto where = loc(v.location);
  const auto id = name(*scope) + ".$for_" + id2string(where.get_line()) + "_" +
                  id2string(where.get_column()) + "." + std::string(v.name);
  const auto t = type(v.getType());
  symbolt index(id, t, ID_Verilog);
  index.base_name = std::string(v.name);
  index.module = root;
  index.location = where;
  index.is_lvalue = true;
  if(object.symbols.add(index))
    fail(v.location, "duplicate loop index " + id);
  const auto ref = index.symbol_expr();
  locals.emplace(&v, ref);
  auto initialization =
    item(ID_verilog_blocking_assign, {ref, literal(first.integer(), t)});
  auto result = item(
    ID_for,
    {item(ID_empty_string, {std::move(initialization)}),
     boolean(expr(*loop.stopExpr)),
     item(ID_postincrement, {ref}),
     stmt(loop.body)});
  locals.erase(&v);
  return result.with_source_location(loc(at));
}

exprt compilert::declare_automatic(const VariableSymbol &v)
{
  if(v.getType().isString())
    fail(v.location, "unsupported runtime string variable");
  if(
    expanded_indices.empty() || v.lifetime != VariableLifetime::Automatic ||
    !v.getType().isSimpleBitVector())
    fail(v.location, "unsupported procedural local declaration");

  auto t = type(v.getType(), v.location);
  t.set("#vv_cycle_local", true);
  const auto id = name(*scope) + ".$local_" +
                  std::to_string(next_automatic_local++) + "." +
                  std::string(v.name);
  symbolt symbol(id, t, ID_Verilog);
  symbol.base_name = std::string(v.name);
  symbol.module = root;
  symbol.location = loc(v.location);
  symbol.is_lvalue = true;
  if(object.symbols.add(symbol))
    fail(v.location, "duplicate automatic local " + id);
  auto [entry, inserted] = automatic_locals.emplace(
    &v, automatic_localt{symbol.symbol_expr(), false, {}});
  if(!inserted)
    fail(v.location, "automatic local scope overlaps a previous declaration");
  auto &local = entry->second;
  if(const auto *init = v.getInitializer())
  {
    auto value = expr(*init);
    local.constant = eval_known(*init);
    local.assigned = true;
    return item(ID_verilog_blocking_assign, {local.ref, std::move(value)})
      .with_source_location(symbol.location);
  }
  return item(ID_skip).with_source_location(symbol.location);
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
    const auto entered = automatic_locals;
    r = stmt(block.body);
    std::erase_if(
      automatic_locals,
      [&](const auto &entry) { return !entered.count(entry.first); });
  }
  break;
  case StatementKind::ImmediateAssertion:
    r = immediate(s.as<ImmediateAssertionStatement>(), nullptr);
    break;
  case StatementKind::VariableDeclaration:
    return declare_automatic(s.as<VariableDeclStatement>().symbol);
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
      const auto &v = list[n]->as<VariableDeclStatement>().symbol;
      const auto *loop =
        n + 1 < list.size() ? list[n + 1]->as_if<ForLoopStatement>() : nullptr;
      if(loop && loop->loopVars.size() == 1 && loop->loopVars.front() == &v)
      {
        r.operands().push_back(counted_loop(v, *loop));
        ++n;
      }
      else
        r.operands().push_back(declare_automatic(v));
    }
    break;
  }
  case StatementKind::Break:
    if(!expanded_indices.empty())
      fail(at, "unsupported break in a loop with automatic locals");
    r = item(ID_break);
    break;
  case StatementKind::Case:
  {
    if(!automatic_locals.empty())
      fail(at, "unsupported case with active automatic locals");
    const auto &c = s.as<CaseStatement>();
    if(
      (c.condition != CaseStatementCondition::Normal &&
       c.condition != CaseStatementCondition::WildcardJustZ) ||
      c.check != UniquePriorityCheck::None)
      fail(at, "unsupported case matching mode");
    const bool casez = c.condition == CaseStatementCondition::WildcardJustZ;
    r = item(casez ? ID_verilog_casez : ID_verilog_case, {expr(c.expr)});
    for(auto &group : c.items)
    {
      auto labels = item(ID_empty_string);
      for(auto *x : group.expressions)
      {
        if(!casez)
        {
          labels.operands().push_back(expr(*x));
          continue;
        }
        EvalContext context(*scope);
        auto pattern = x->eval(context);
        if(!pattern.isInteger())
          fail(x->sourceRange.start(), "requires a constant casez pattern");
        // Retain literal Z and X bits until the RTL builder makes the mask.
        auto &v = pattern.integer();
        auto t = v.isSigned()
                   ? typet(verilog_signedbv_typet(v.getBitWidth()))
                   : typet(verilog_unsignedbv_typet(v.getBitWidth()));
        labels.operands().push_back(
          literal(v, v.hasUnknown() ? t : type(*x->type)));
      }
      r.operands().push_back(item(ID_case_item, {labels, stmt(*group.stmt)}));
    }
    if(c.defaultCase)
      r.operands().push_back(
        item(ID_case_item, {item(ID_default), stmt(*c.defaultCase)}));
    break;
  }
  case StatementKind::Timed:
  {
    if(!expanded_indices.empty())
      fail(at, "unsupported timing in a loop with automatic locals");
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
    const auto &test = *c.conditions[0].expr;
    EvalContext context(*scope);
    const auto constant = test.eval(context);
    if(constant.isInteger() && !constant.integer().hasUnknown())
    {
      if(constant.isTrue())
        return stmt(c.ifTrue);
      if(c.ifFalse)
        return stmt(*c.ifFalse);
      r = item(ID_skip);
      break;
    }
    auto condition = boolean(expr(test));
    const auto entered = automatic_locals;
    auto when_true = stmt(c.ifTrue);
    const auto after_true = std::move(automatic_locals);
    automatic_locals = entered;
    r = item(ID_if, {std::move(condition), std::move(when_true)});
    if(c.ifFalse)
      r.operands().push_back(stmt(*c.ifFalse));
    for(const auto &entry : entered)
    {
      const auto *symbol = entry.first;
      auto &after = automatic_locals.at(symbol);
      const auto &other = after_true.at(symbol);
      after.assigned &= other.assigned;
      if(!after.assigned || !(after.constant == other.constant))
        after.constant = {};
    }
    break;
  }
  case StatementKind::ExpressionStatement:
  {
    auto &e = s.as<ExpressionStatement>().expr;
    if(e.kind == ExpressionKind::Call)
      fail(
        at,
        "unsupported procedural call " +
          std::string(e.as<CallExpression>().getSubroutineName()));
    if(e.kind == ExpressionKind::UnaryOp)
    {
      auto &u = e.as<UnaryExpression>();
      if(
        u.op == UnaryOperator::Postincrement && expanded_indices.empty() &&
        !locals.count(u.operand().getSymbolReference()))
      {
        r = item(ID_postincrement, {expr(u.operand())});
        break;
      }
    }
    if(e.kind != ExpressionKind::Assignment)
      fail(at, "expected assignment");
    auto &a = e.as<AssignmentExpression>();
    const auto *target = a.left().getSymbolReference();
    if(locals.count(target) || expanded_indices.count(target))
      fail(at, "write to a statically expanded loop index");
    if(auto entry = automatic_locals.find(target);
       entry != automatic_locals.end())
    {
      if(
        a.left().kind != ExpressionKind::NamedValue || a.isNonBlocking() ||
        a.op || a.timingControl)
        fail(at, "unsupported assignment to an automatic local");
      auto rhs = expr(a.right());
      auto &local = entry->second;
      local.constant = eval_known(a.right());
      local.assigned = true;
      r = item(ID_verilog_blocking_assign, {local.ref, std::move(rhs)});
      break;
    }
    const auto contains_local_target =
      [&](auto &&self, const Expression &lhs) -> bool
    {
      if(const auto *concat = lhs.as_if<ConcatenationExpression>())
        return std::any_of(
          concat->operands().begin(),
          concat->operands().end(),
          [&](const Expression *operand) { return self(self, *operand); });
      const auto *symbol = lhs.getSymbolReference();
      return automatic_locals.count(symbol) || expanded_indices.count(symbol);
    };
    if(contains_local_target(contains_local_target, a.left()))
      fail(at, "unsupported assignment target containing a procedural local");
    if(a.op)
    {
      if(
        (*a.op != BinaryOperator::Add && *a.op != BinaryOperator::Subtract) ||
        a.isNonBlocking() || a.timingControl)
        fail(at, "unsupported compound assignment");
      if(!a.left().type->isIntegral() || !static_compound_target(a.left()))
        fail(
          a.left().sourceRange.start(),
          "unsupported compound assignment target");
    }
    auto lhs = expr(a.left());
    // The typed RHS reads this target in the current procedural state.
    auto rhs = expr(a.right(), a.op ? &lhs : nullptr);
    r = item(
      a.isNonBlocking() ? ID_verilog_non_blocking_assign
                        : ID_verilog_blocking_assign,
      {std::move(lhs), std::move(rhs)});
    break;
  }
  default:
    fail(at, "unsupported statement " + std::string(toString(s.kind)));
  }
  r.add_source_location() = loc(at);
  return r;
}
} // namespace vv
