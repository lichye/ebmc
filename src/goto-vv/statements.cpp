#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/Operator.h>

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

const Expression &strip_integral_conversions(const Expression &e)
{
  const auto *operand = &e;
  while(const auto *cast = operand->as_if<ConversionExpression>())
  {
    if(
      !cast->isImplicit() || !cast->type->isIntegral() ||
      !cast->operand().type->isIntegral())
      break;
    operand = &cast->operand();
  }
  return *operand;
}

bool is_unit_assignment_step(
  const AssignmentExpression &step,
  const VariableSymbol &index,
  BinaryOperator op)
{
  if(
    !index.getType().isSimpleBitVector() || step.isNonBlocking() ||
    step.timingControl || step.op ||
    step.left().kind != ExpressionKind::NamedValue ||
    step.left().getSymbolReference() != &index)
    return false;

  const auto *sum =
    strip_integral_conversions(step.right()).as_if<BinaryExpression>();
  if(!sum || sum->op != op || !sum->type->isIntegral())
    return false;

  const auto &operand = strip_integral_conversions(sum->left());
  if(
    operand.kind != ExpressionKind::NamedValue ||
    operand.getSymbolReference() != &index || !sum->right().type->isIntegral())
    return false;

  EvalContext context(index);
  const auto amount = sum->right().eval(context);
  return amount.isInteger() && amount.integer().as<int64_t>() == 1;
}

// A later initial statement can be checked at compile time only if the
// preceding statements cannot suspend the process or leave the block.
bool completes_at_initial_entry(const Statement &s)
{
  if(const auto *block = s.as_if<BlockStatement>())
    return block->blockKind == StatementBlockKind::Sequential &&
           completes_at_initial_entry(block->body);
  if(const auto *list = s.as_if<StatementList>())
    return std::all_of(
      list->list.begin(),
      list->list.end(),
      [](const auto *child) { return completes_at_initial_entry(*child); });
  if(const auto *branch = s.as_if<ConditionalStatement>())
    return completes_at_initial_entry(branch->ifTrue) &&
           (!branch->ifFalse || completes_at_initial_entry(*branch->ifFalse));
  if(const auto *statement = s.as_if<ExpressionStatement>())
  {
    const auto *assignment = statement->expr.as_if<AssignmentExpression>();
    return !assignment || !assignment->timingControl;
  }
  return s.kind == StatementKind::Empty ||
         s.kind == StatementKind::VariableDeclaration ||
         s.kind == StatementKind::ImmediateAssertion;
}

bool is_automatic_packed_struct(const VariableSymbol &v)
{
  return v.lifetime == VariableLifetime::Automatic &&
         v.getType().getCanonicalType().kind == SymbolKind::PackedStructType;
}

bool is_module_variable(const Symbol &symbol)
{
  const auto *parent = symbol.getParentScope();
  return symbol.kind == SymbolKind::Variable && parent &&
         parent->asSymbol().kind == SymbolKind::InstanceBody;
}

class loop_expansion_visitort
  : public ASTVisitor<loop_expansion_visitort, VisitFlags::AllGood>
{
  const VariableSymbol &index;
  bool in_always_comb;
  bool packed_index = false;
  std::set<const VariableSymbol *> declared;
  std::set<const VariableSymbol *> indices;

  void check_bound(const Expression &bound)
  {
    bound.visitSymbolReferences([&](const Expression &, const Symbol &reference)
                                { packed_index |= &reference == &index; });
  }

public:
  loop_expansion_visitort(const VariableSymbol &index, bool in_always_comb)
    : index(index), in_always_comb(in_always_comb)
  {
  }

  void handle(const VariableDeclStatement &declaration)
  {
    if(
      in_always_comb ||
      declaration.symbol.lifetime == VariableLifetime::Automatic)
      declared.insert(&declaration.symbol);
  }

  void handle(const ForLoopStatement &loop)
  {
    indices.insert(loop.loopVars.begin(), loop.loopVars.end());
    visitDefault(loop);
  }

  void handle(const ElementSelectExpression &select)
  {
    if(const auto *array =
         select.value().type->getCanonicalType().as_if<PackedArrayType>();
       array && needs_packed_element_layout(*array))
      check_bound(select.selector());
    visitDefault(select);
  }

  void handle(const RangeSelectExpression &select)
  {
    if(const auto *array =
         select.value().type->getCanonicalType().as_if<PackedArrayType>();
       array && has_packed_elements(*array))
    {
      check_bound(select.left());
      check_bound(select.right());
    }
    visitDefault(select);
  }

  bool needs_expansion() const
  {
    return packed_index || std::any_of(
                             declared.begin(),
                             declared.end(),
                             [&](const auto *v) { return !indices.count(v); });
  }
};
} // namespace

void compilert::require_read_only(
  const Expression &expr,
  const char *diagnostic) const
{
  // Constant evaluation would perform these writes on temporary copies.
  expr.visit(makeVisitor(
    [&](auto &, const AssignmentExpression &e)
    { fail(e.sourceRange.start(), diagnostic); },
    [&](auto &visitor, const UnaryExpression &e)
    {
      if(OpInfo::isLValue(e.op))
        fail(e.sourceRange.start(), diagnostic);
      visitor.visitDefault(e);
    }));
}

void compilert::check_diagnostic_argument(const Expression &argument) const
{
  // Pure arguments contribute only to the discarded message. Reject calls
  // whose effect is unknown, including functions hidden in a larger expression.
  require_read_only(argument, "unsupported write in diagnostic argument");
  argument.visit(makeVisitor(
    [&](auto &visitor, const CallExpression &call)
    {
      const auto task = call.getSubroutineName();
      if(
        !call.isSystemCall() ||
        (task != "$signed" && task != "$unsigned" && task != "$bits" &&
         task != "$clog2" && task != "$sformatf"))
        fail(
          call.sourceRange.start(),
          "unsupported call in diagnostic argument " + std::string(task));
      visitor.visitDefault(call);
    },
    [&](auto &, const NewClassExpression &allocation)
    {
      fail(
        allocation.sourceRange.start(),
        "unsupported class construction in diagnostic argument");
    },
    [&](auto &, const NewCovergroupExpression &allocation)
    {
      fail(
        allocation.sourceRange.start(),
        "unsupported covergroup construction in diagnostic argument");
    }));
}

bool compilert::assert_diagnostic(std::string_view task) const
{
  return (task == "$warning" && assert_warnings) ||
         (task == "$error" && assert_errors) ||
         (task == "$fatal" && assert_fatals);
}

exprt compilert::diagnostic_statement(const CallExpression &call)
{
  const auto task = call.getSubroutineName();
  const auto at = call.sourceRange.start();
  const auto selected = assert_diagnostic(task);
  if(!call.isSystemCall() || (task != "$info" && task != "$warning" && !selected))
    fail(at, "unsupported procedural call " + std::string(task));

  // The RTL builder registers one property value per identifier. Repeated
  // calls need an aggregation rule before they can form one property.
  if(selected && (function || !locals.empty() || !expanded_indices.empty()))
    fail(
      at, "diagnostic assertion in a runtime function or loop is unsupported");

  auto result = item(ID_block);
  for(const auto *argument : call.arguments())
  {
    const auto &effect = strip_integral_conversions(*argument);
    const auto *assignment = effect.as_if<AssignmentExpression>();
    const auto *unary = effect.as_if<UnaryExpression>();
    const auto *function_call = effect.as_if<CallExpression>();
    const bool writes = assignment ||
                        (unary && unary->op == UnaryOperator::Postincrement);
    const bool calls_user_code = function_call && !function_call->isSystemCall();
    if(!writes && !calls_user_code)
    {
      check_diagnostic_argument(*argument);
      continue;
    }

    // We can retain a standalone effect with the existing statement
    // lowering. Do not guess at the order of multiple effectful arguments.
    const auto where = effect.sourceRange.start();
    if(!result.operands().empty())
      fail(where, "multiple side effects in diagnostic arguments");
    if(calls_user_code)
    {
      const auto *callee =
        std::get<const SubroutineSymbol *>(function_call->subroutine);
      if(active_functions.count(callee))
        fail(where, "recursive call in diagnostic argument");
      for(const auto *actual : function_call->arguments())
        check_diagnostic_argument(*actual);
      auto value = runtime_call(*function_call);
      auto sink = temporary_local(value.type(), where, "$diagnostic_argument_");
      result.operands().push_back(
        item(ID_verilog_blocking_assign, {std::move(sink), std::move(value)})
          .with_source_location(loc(where)));
      continue;
    }

    if(assignment)
    {
      if(assignment->isNonBlocking() || assignment->timingControl)
        fail(where, "unsupported assignment in diagnostic argument");
      check_diagnostic_argument(assignment->left());
      check_diagnostic_argument(assignment->right());
    }
    else
      check_diagnostic_argument(unary->operand());

    result.operands().push_back(
      stmt(ExpressionStatement(effect, effect.sourceRange)));
  }

  if(selected)
  {
    const auto site = sm.getFullyExpandedLoc(at);
    const auto where = loc(site);
    const auto stem = std::string(task) + "_" + id2string(where.get_line()) +
                      "_" + id2string(where.get_column());
    const auto prefix = name(*scope) + ".";
    auto base = stem;
    for(unsigned occurrence = 2; object.symbols.lookup(prefix + base);
        ++occurrence)
      base = stem + "_" + std::to_string(occurrence);
    result.operands().push_back(
      procedural_assertion(site, base, prefix + base, false_exprt{}, task));
  }

  if(result.operands().empty())
    return item(ID_skip).with_source_location(loc(at));
  return result.with_source_location(loc(at));
}

symbol_exprt compilert::temporary_local(
  typet type,
  SourceLocation at,
  std::string_view stem)
{
  type.set("#vv_cycle_local", true);
  const auto prefix = function ? function_identifier : name(*scope);
  const auto id =
    prefix + "." + std::string(stem) + std::to_string(next_procedural_local++);
  symbolt symbol(id, type, ID_Verilog);
  symbol.module = root;
  symbol.location = loc(at);
  symbol.is_lvalue = true;
  if(object.symbols.add(symbol))
    fail(at, "duplicate procedural local " + id);
  return symbol.symbol_expr();
}

exprt compilert::procedural_assertion(
  SourceLocation at,
  const std::string &base,
  const std::string &id,
  exprt condition,
  std::string_view diagnostic_task)
{
  const auto where = loc(at);
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
    {std::move(condition), item(ID_verilog_action_then, {item(ID_skip)})});
  result.set(ID_identifier, id);
  result.set(ID_base_name, base);
  result.add_source_location() = where;
  auto &info = object.metadata.add("properties").add(id);
  info.id("property");
  info.set("sampling", "procedural");
  info.set("origin", diagnostic_task.empty() ? "source" : "diagnostic-task");
  if(!diagnostic_task.empty())
    info.set("diagnostic_task", std::string(diagnostic_task));
  info.set("kind", ID_verilog_immediate_assert);
  info.add(ID_C_source_location) = where;
  return result;
}

void compilert::merge_procedural_locals(
  const decltype(procedural_locals) &entered,
  const decltype(procedural_locals) &other)
{
  for(const auto &entry : entered)
  {
    const auto *symbol = entry.first;
    auto &local = procedural_locals.at(symbol);
    const auto &alternative = other.at(symbol);
    local.assigned &= alternative.assigned;
    if(!local.assigned || !(local.constant == alternative.constant))
      local.constant = {};
  }
}

bool compilert::initial_parameter_check(
  const ImmediateAssertionStatement &a) const
{
  if((a.ifTrue && a.ifTrue->kind != StatementKind::Empty) || !a.ifFalse)
    return false;
  const auto *action = a.ifFalse->as_if<ExpressionStatement>();
  const auto *call = action ? action->expr.as_if<CallExpression>() : nullptr;
  if(
    !call || !call->isSystemCall() || call->getSubroutineName() != "$fatal" ||
    has_checked_call(a.cond))
    return false;

  // A fresh context only sees elaborated constants, including instance
  // parameters. The process's runtime values are absent.
  EvalContext context(*scope);
  const auto value = a.cond.eval(context);
  if(!value || !context.getAllDiagnostics().empty())
    return false;
  if(value.isTrue())
    return true;

  auto diagnostic =
    "initial parameter assertion failed in " + scope->getHierarchicalPath();
  const auto args = call->arguments();
  if(args.size() == 2)
  {
    if(const auto *literal = args[1]->as_if<StringLiteral>();
       literal && !literal->getValue().empty() &&
       literal->getValue().find('%') == std::string_view::npos)
      diagnostic += ": " + std::string(literal->getValue());
  }
  fail(a.sourceRange.start(), diagnostic);
}

exprt compilert::immediate(
  const ImmediateAssertionStatement &a,
  const Symbol *label,
  bool at_initial_entry)
{
  auto at = a.sourceRange.start();
  if(a.assertionKind != AssertionKind::Assert || a.isDeferred)
    fail(at, "unsupported procedural assertion kind or timing");
  if(at_initial_entry && initial_parameter_check(a))
    return item(ID_skip).with_source_location(loc(at));
  auto condition = boolean(expr(a.cond));
  auto lower_action = [&](const Statement *action)
  {
    if(!action || action->kind == StatementKind::Empty)
      return item(ID_skip);
    if(const auto *statement = action->as_if<ExpressionStatement>())
    {
      if(const auto *call = statement->expr.as_if<CallExpression>();
         call && call->isSystemCall())
      {
        const auto task = call->getSubroutineName();
        if(task == "$info" || task == "$warning" || assert_diagnostic(task))
          return diagnostic_statement(*call);
        // Preserve the existing representation of a simple assertion error
        // action unless it was explicitly selected as another property.
        if(
          task == "$error" &&
          std::all_of(
             call->arguments().begin(),
             call->arguments().end(),
             [](const Expression *arg)
             {
               return arg->kind == ExpressionKind::StringLiteral ||
                      arg->kind == ExpressionKind::IntegerLiteral ||
                      arg->kind == ExpressionKind::NamedValue;
             }))
          return item(ID_skip);
      }
    }
    fail(at, "unsupported procedural assertion action");
  };
  const auto entered = procedural_locals;
  auto when_true = lower_action(a.ifTrue);
  const auto after_true = std::move(procedural_locals);
  procedural_locals = entered;
  auto when_false = lower_action(a.ifFalse);
  merge_procedural_locals(entered, after_true);

  const auto where = loc(at);
  const auto base = label ? std::string(label->name)
                          : "$assert_" + id2string(where.get_line()) + "_" +
                              id2string(where.get_column());
  const auto id = label ? name(*label) : name(*scope) + "." + base;
  if(when_true.id() == ID_skip && when_false.id() == ID_skip)
    return procedural_assertion(at, base, id, std::move(condition));

  // The original assertion and its action use the same sampled condition.
  const auto ref = temporary_local(bool_typet(), at, "$assert_condition_");
  return item(
    ID_block,
    {item(ID_verilog_blocking_assign, {ref, std::move(condition)}),
     procedural_assertion(at, base, id, ref),
     item(ID_if, {ref, std::move(when_true), std::move(when_false)})})
    .with_source_location(where);
}

std::optional<async_resett> match_async_reset(const TimedStatement &timed)
{
  const auto *list = timed.timing.as_if<EventListControl>();
  if(!list || list->events.size() != 2)
    return {};
  const auto *event = list->events[1]->as_if<SignalEventControl>();
  if(
    !event ||
    (event->edge != EdgeKind::PosEdge && event->edge != EdgeKind::NegEdge) ||
    event->iffCondition || event->expr.kind != ExpressionKind::NamedValue ||
    !event->expr.type->isIntegral() || event->expr.type->getBitWidth() != 1)
    return {};
  const auto *signal = &event->expr.as<NamedValueExpression>().symbol;
  const Statement *body = &timed.stmt;
  while(const auto *block = body->as_if<BlockStatement>())
    body = &block->body;
  const auto *condition = body->as_if<ConditionalStatement>();
  if(
    !condition || condition->conditions.size() != 1 ||
    condition->conditions[0].pattern)
    return {};

  const bool active_low = event->edge == EdgeKind::NegEdge;
  const Expression *test = &*condition->conditions[0].expr;
  if(active_low)
  {
    const auto *negation = test->as_if<UnaryExpression>();
    if(!negation || negation->op != UnaryOperator::LogicalNot)
      return {};
    test = &negation->operand();
  }
  if(
    test->kind != ExpressionKind::NamedValue ||
    test->getSymbolReference() != signal)
    return {};
  return async_resett{event, signal, condition, active_low};
}

// Preserve both native operands. Only the first edge samples properties.
exprt compilert::clock_reset_block(const TimedStatement &block)
{
  const auto at = block.timing.sourceRange.start();
  const auto events = block.timing.as<EventListControl>().events;
  if(events.size() != 2)
    fail(at, "expected a clock and one reset edge");
  const auto clock_id = clock(events[0], at);
  const auto reset = match_async_reset(block);
  if(!reset)
    fail(at, "expected a reset edge matching the outer if");

  const auto &clk = events[0]->as<SignalEventControl>();
  auto event = node(
    ID_verilog_event,
    bool_typet(),
    {clock_edge(clk.expr, clock_id),
     node(
       reset->active_low ? ID_negedge : ID_posedge,
       bool_typet(),
       {expr(reset->event->expr)})});
  auto guard = item(ID_event_guard, {std::move(event), stmt(block.stmt)});
  guard.add_source_location() = loc(at);
  return guard;
}

std::optional<exprt> compilert::named_clocked_loop(const BlockStatement &block)
{
  if(
    !in_posedge_process || function || !block.blockSymbol ||
    block.blockSymbol->name.empty() ||
    block.blockKind != StatementBlockKind::Sequential)
    return {};
  const auto *body = block.body.as_if<StatementList>();
  if(!body || body->list.size() != 2)
    return {};
  const auto *declaration = body->list[0]->as_if<VariableDeclStatement>();
  const auto *loop = body->list[1]->as_if<ForLoopStatement>();
  if(!declaration || !loop)
    return {};
  const auto &index = declaration->symbol;
  const auto *type =
    index.getType().getCanonicalType().as_if<PredefinedIntegerType>();
  if(
    !type || type->integerKind != PredefinedIntegerType::Integer ||
    index.getInitializer())
    return {};
  const auto *init =
    loop->initializers.size() == 1
      ? loop->initializers.front()->as_if<AssignmentExpression>()
      : nullptr;
  if(
    !loop->loopVars.empty() || !init || init->isNonBlocking() ||
    init->timingControl || init->op ||
    init->left().kind != ExpressionKind::NamedValue ||
    init->left().getSymbolReference() != &index)
    fail(
      loop->sourceRange.start(),
      "requires a direct blocking initializer for the named block loop index");

  // The counter is unused before this initializer or after the loop. Keep
  // only its typed iteration values; a hierarchical use outside this body
  // has no symbol binding and is diagnosed instead of becoming state.
  return counted_loop(index, *loop, init);
}

exprt compilert::counted_loop(
  const VariableSymbol &v,
  const ForLoopStatement &loop,
  const AssignmentExpression *external_init)
{
  const auto at = loop.sourceRange.start();
  const auto *init = external_init ? &external_init->right() : v.getInitializer();
  if(
    !init || !loop.stopExpr || (!external_init && !loop.initializers.empty()) ||
    loop.steps.size() != 1)
    fail(at, "requires a statically bounded counted loop");
  const auto &step = *loop.steps.front();
  const auto step_at = step.sourceRange.start();
  const auto *assignment = step.as_if<AssignmentExpression>();
  bool descending = false;
  if(assignment)
  {
    descending = function && is_unit_assignment_step(
                               *assignment, v, BinaryOperator::Subtract);
    if(
      !descending &&
      !is_unit_assignment_step(*assignment, v, BinaryOperator::Add))
      fail(step_at, "expected a blocking unit increment of the loop-local index");
  }
  else
  {
    const auto *increment = step.as_if<UnaryExpression>();
    descending =
      function && increment && increment->op == UnaryOperator::Postdecrement;
    if(
      !increment ||
      (!descending && increment->op != UnaryOperator::Postincrement) ||
      increment->operand().getSymbolReference() != &v)
      fail(step_at, "expected an increment of the loop-local index");
  }

  if(
    function || external_init ||
    std::any_of(
      expanded_indices.begin(),
      expanded_indices.end(),
      [](const auto &entry) { return is_module_variable(*entry.first); }))
  {
    require_read_only(*init, "unsupported write in a loop initializer");
    require_read_only(*loop.stopExpr, "unsupported write in a loop guard");
  }

  reject_checked_call(*init);
  reject_checked_call(*loop.stopExpr);
  reject_checked_call(step);
  EvalContext context(v);
  for(const auto &[formal, value] : function_constants)
    context.createLocal(formal, value);
  for(const auto &[index, value] : expanded_indices)
    context.createLocal(&index->as<VariableSymbol>(), value);
  const auto first = init->eval(context);
  if(!first.isInteger() || first.integer().hasUnknown())
    fail(init->sourceRange.start(), "nonconstant loop initializer");
  context.createLocal(&v, first);
  const bool expand_nested = function || external_init ||
                             !expanded_indices.empty() ||
                             !procedural_locals.empty();
  loop_expansion_visitort visitor(v, in_always_comb);
  if(!expand_nested)
    loop.body.visit(visitor);
  const bool expand = expand_nested || visitor.needs_expansion();
  auto expanded = item(ID_block);
  const auto *external_symbol = external_init && is_module_variable(v)
                                  ? &object.symbols.lookup_ref(name(v))
                                  : nullptr;
  const auto write_index = [&](const ConstantValue &value, SourceLocation where)
  {
    auto write = item(
      ID_verilog_blocking_assign,
      {external_symbol->symbol_expr(),
       literal(value.integer(), external_symbol->type)});
    write.add_source_location() = loc(where);
    expanded.operands().push_back(std::move(write));
  };
  if(external_symbol)
    write_index(first, external_init->sourceRange.start());
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
    std::optional<SVInt> expected;
    if(assignment)
    {
      expected = context.findLocal(&v)->integer();
      if(descending)
        --*expected;
      else
        ++*expected;
    }
    const auto updated = step.eval(context);
    if(!updated.isInteger())
      fail(step_at, "cannot evaluate loop step");
    if(expected)
    {
      // Check the stored value after Slang's assignment conversions.
      const auto &actual = context.findLocal(&v)->integer();
      if(
        actual.hasUnknown() || actual.getBitWidth() != expected->getBitWidth() ||
        actual.isSigned() != expected->isSigned() || actual != *expected)
        fail(step_at, "loop step does not preserve a typed unit change");
    }
    if(external_symbol)
      write_index(*context.findLocal(&v), step_at);
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
  // The proven assignment step can use the original RTL unroller. In the
  // Boolean representation, increment modulo two is logical negation.
  auto increment = assignment && t.id() == ID_bool
                     ? item(ID_verilog_blocking_assign, {ref, not_exprt(ref)})
                     : item(ID_postincrement, {ref});
  increment.add_source_location() = loc(step_at);
  auto result = item(
    ID_for,
    {item(ID_empty_string, {std::move(initialization)}),
     boolean(expr(*loop.stopExpr)),
     std::move(increment),
     stmt(loop.body)});
  locals.erase(&v);
  return result.with_source_location(loc(at));
}

exprt compilert::declare_local(const VariableSymbol &v)
{
  if(function)
    return declare_function_local(v);
  if(v.getType().isString())
    fail(v.location, "unsupported runtime string variable");
  const bool automatic_in_loop =
    !expanded_indices.empty() && v.lifetime == VariableLifetime::Automatic;
  const auto *init = v.getInitializer();
  const bool packed_comb_local =
    in_always_comb && is_automatic_packed_struct(v);
  if(
    (!automatic_in_loop && !in_always_comb) ||
    (!v.getType().isSimpleBitVector() && !packed_comb_local))
    fail(v.location, "unsupported procedural local declaration");
  if(!automatic_in_loop && init)
    fail(v.location, "unsupported initializer for a combinational local");

  auto t = type(v.getType(), v.location);
  t.set("#vv_cycle_local", true);
  const auto id = name(*scope) + ".$local_" +
                  std::to_string(next_procedural_local++) + "." +
                  std::string(v.name);
  symbolt symbol(id, t, ID_Verilog);
  symbol.base_name = std::string(v.name);
  symbol.module = root;
  symbol.location = loc(v.location);
  symbol.is_lvalue = true;
  if(object.symbols.add(symbol))
    fail(v.location, "duplicate procedural local " + id);
  auto [entry, inserted] = procedural_locals.emplace(
    &v, procedural_localt{symbol.symbol_expr(), false, !automatic_in_loop, {}});
  if(!inserted)
    fail(v.location, "procedural local scope overlaps a previous declaration");
  auto &local = entry->second;
  if(init)
  {
    auto value = expr(*init);
    local.constant = eval_known(*init);
    local.assigned = true;
    return item(ID_verilog_blocking_assign, {local.ref, std::move(value)})
      .with_source_location(symbol.location);
  }
  return item(ID_skip).with_source_location(symbol.location);
}

bool compilert::is_function_variable(const Symbol *symbol) const
{
  if(!symbol || !VariableSymbol::isKind(symbol->kind))
    return false;
  for(auto *parent = symbol->getParentScope(); parent;
      parent = parent->asSymbol().getParentScope())
    if(&parent->asSymbol() == function)
      return symbol->as<VariableSymbol>().lifetime ==
             VariableLifetime::Automatic;
  return false;
}

std::string compilert::function_variable_name(const Symbol &symbol) const
{
  if(
    symbol.kind == SymbolKind::FormalArgument ||
    &symbol == function->returnValVar)
    return function_identifier + "." + std::string(symbol.name);
  return function_identifier + ".$local_" +
         std::to_string(symbol.location.buffer().getId()) + "_" +
         std::to_string(symbol.location.offset()) + "." +
         std::string(symbol.name);
}

exprt compilert::declare_function_local(const VariableSymbol &v)
{
  const auto &local_type = v.getType().getCanonicalType();
  const auto *array = local_type.as_if<FixedSizeUnpackedArrayType>();
  // Use the ordinary unpacked array and select IR in the native call frame.
  if(
    !is_function_variable(&v) ||
    !(local_type.isIntegral() || (array && array->elementType.isIntegral())))
    fail(v.location, "unsupported runtime function local declaration");
  const auto id = function_variable_name(v);
  if(!object.symbols.lookup(id))
    add(v, type(v.getType(), v.location), id).is_lvalue = true;
  const auto &local = object.symbols.lookup_ref(id);
  if(const auto *init = v.getInitializer())
    return item(
             ID_verilog_blocking_assign,
             {local.symbol_expr(), evaluated_expr(*init)})
      .with_source_location(local.location);
  return item(ID_skip).with_source_location(local.location);
}

void compilert::require_empty_final(const Statement &s) const
{
  switch(s.kind)
  {
  case StatementKind::Empty:
    return;
  case StatementKind::Block:
  {
    const auto &block = s.as<BlockStatement>();
    if(block.blockKind != StatementBlockKind::Sequential)
      break;
    require_empty_final(block.body);
    return;
  }
  case StatementKind::List:
    for(const auto *statement : s.as<StatementList>().list)
      require_empty_final(*statement);
    return;
  case StatementKind::Conditional:
  {
    const auto &conditional = s.as<ConditionalStatement>();
    if(conditional.conditions.size() != 1 || conditional.conditions[0].pattern)
      fail(s.sourceRange.start(), "unsupported condition in final");
    const auto &test = *conditional.conditions[0].expr;
    const auto value = eval_known(test);
    if(!value.isInteger() || value.integer().hasUnknown())
      fail(test.sourceRange.start(), "final condition must be a known constant");
    if(value.isTrue())
      require_empty_final(conditional.ifTrue);
    else if(conditional.ifFalse)
      require_empty_final(*conditional.ifFalse);
    return;
  }
  case StatementKind::ExpressionStatement:
    if(const auto *call =
         s.as<ExpressionStatement>().expr.as_if<CallExpression>())
    {
      const auto task = call->getSubroutineName();
      if(
        call->isSystemCall() && (task == "$info" || task == "$warning") &&
        !assert_diagnostic(task))
      {
        for(const auto *argument : call->arguments())
          check_diagnostic_argument(*argument);
        return;
      }
      fail(
        call->sourceRange.start(),
        "unsupported reachable final call " + std::string(task));
    }
    break;
  default:
    break;
  }
  fail(
    s.sourceRange.start(),
    "unsupported reachable final statement " + std::string(toString(s.kind)));
}

exprt compilert::stmt(const Statement &s, bool at_initial_entry)
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
    if(function && block.blockKind != StatementBlockKind::Sequential)
      fail(at, "unsupported runtime function block");
    at_initial_entry &= block.blockKind == StatementBlockKind::Sequential;
    if(
      !function && block.blockSymbol && !block.blockSymbol->name.empty() &&
      block.body.kind == StatementKind::ImmediateAssertion)
      return immediate(
        block.body.as<ImmediateAssertionStatement>(),
        block.blockSymbol,
        at_initial_entry);
    if(auto loop = named_clocked_loop(block))
      return std::move(*loop);
    const auto entered = procedural_locals;
    r = stmt(block.body, at_initial_entry);
    std::erase_if(
      procedural_locals,
      [&](const auto &entry)
      {
        if(entered.count(entry.first))
          return false;
        if(entry.second.require_full_assignment && !entry.second.assigned)
          fail(
            entry.first->location,
            "combinational local is not assigned on every path");
        return true;
      });
  }
  break;
  case StatementKind::ImmediateAssertion:
    if(function)
      fail(at, "unsupported assertion in a runtime function");
    r = immediate(
      s.as<ImmediateAssertionStatement>(), nullptr, at_initial_entry);
    break;
  case StatementKind::Return:
  {
    const auto *value = s.as<ReturnStatement>().expr;
    if(!function || !value)
      fail(at, "unsupported return statement");
    r = verilog_returnt(evaluated_expr(*value));
    break;
  }
  case StatementKind::VariableDeclaration:
    return declare_local(s.as<VariableDeclStatement>().symbol);
  case StatementKind::List:
  {
    r = item(ID_block);
    auto list = s.as<StatementList>().list;
    for(size_t n = 0; n < list.size(); n++)
    {
      if(list[n]->kind != StatementKind::VariableDeclaration)
      {
        r.operands().push_back(stmt(*list[n], at_initial_entry));
        if(at_initial_entry)
          at_initial_entry = completes_at_initial_entry(*list[n]);
        continue;
      }
      const auto &v = list[n]->as<VariableDeclStatement>().symbol;
      const auto *loop =
        n + 1 < list.size() ? list[n + 1]->as_if<ForLoopStatement>() : nullptr;
      if(loop && loop->loopVars.size() == 1 && loop->loopVars.front() == &v)
      {
        r.operands().push_back(counted_loop(v, *loop));
        at_initial_entry = false;
        ++n;
      }
      else
        r.operands().push_back(declare_local(v));
    }
    break;
  }
  case StatementKind::ForLoop:
  {
    if(function)
      fail(at, "requires a declared local runtime function loop index");
    const auto &loop = s.as<ForLoopStatement>();
    if(!loop.loopVars.empty() || loop.initializers.size() != 1)
      fail(at, "requires one direct initializer for a module loop index");
    const auto *init =
      loop.initializers.front()->as_if<AssignmentExpression>();
    if(
      !init || init->isNonBlocking() || init->timingControl || init->op ||
      init->left().kind != ExpressionKind::NamedValue)
      fail(
        loop.initializers.front()->sourceRange.start(),
        "requires a direct blocking initializer for a module loop index");
    const auto *target = init->left().getSymbolReference();
    const auto where = init->left().sourceRange.start();
    if(
      !target || !is_module_variable(*target) ||
      !target->as<VariableSymbol>().getType().isSimpleBitVector())
      fail(where, "requires a module variable as the loop index");
    if(expanded_indices.count(target))
      fail(where, "write to a statically expanded loop index");
    const auto *symbol = object.symbols.lookup(name(*target));
    if(!symbol || !symbol->is_lvalue)
      fail(where, "requires a writable module loop index");
    r = counted_loop(target->as<VariableSymbol>(), loop, init);
    break;
  }
  case StatementKind::Break:
    if(function || !expanded_indices.empty())
      fail(at, "unsupported break in a statically expanded loop");
    r = item(ID_break);
    break;
  case StatementKind::Case:
  {
    const auto &c = s.as<CaseStatement>();
    const bool normal = c.condition == CaseStatementCondition::Normal;
    const bool package_case = in_package_function() && normal;
    const bool casez = c.condition == CaseStatementCondition::WildcardJustZ;
    const bool unique = c.check == UniquePriorityCheck::Unique;
    const bool priority = c.check == UniquePriorityCheck::Priority;
    const bool qualified = normal && (unique || priority);
    if(
      !procedural_locals.empty() &&
      (!in_always_comb_procedure || !normal ||
       (c.check != UniquePriorityCheck::None && !unique)))
      fail(at, "unsupported case with active procedural locals");
    if(
      (!normal && !casez) ||
      (c.check != UniquePriorityCheck::None && !qualified))
      fail(at, "unsupported case matching mode");

    const bool check_overlap = assert_case_qualifiers && qualified && unique;
    const bool check_coverage =
      assert_case_qualifiers && qualified && !c.defaultCase;
    if(check_overlap || check_coverage)
    {
      if(
        function || !locals.empty() || !expanded_indices.empty() ||
        in_gated_async)
        fail(
          at,
          "unsupported case qualifier assertion in this procedural context");
    }
    if(check_overlap || check_coverage || !procedural_locals.empty())
    {
      const auto diagnostic =
        check_overlap || check_coverage
          ? "unsupported expression effect in a checked case"
          : "unsupported expression effect in a case with procedural locals";
      require_read_only(c.expr, diagnostic);
      for(const auto &group : c.items)
        for(const auto *label : group.expressions)
          require_read_only(*label, diagnostic);
    }

    // Retain exact X/Z matching when Slang knows the selector and item values,
    // even if a different argument keeps the call itself at run time.
    const auto selector = package_case ? eval_known(c.expr) : ConstantValue{};
    const bool exact =
      selector.isInteger() && std::all_of(
                                c.items.begin(),
                                c.items.end(),
                                [&](const auto &group)
                                {
                                  return std::all_of(
                                    group.expressions.begin(),
                                    group.expressions.end(),
                                    [&](const Expression *label)
                                    { return eval_known(*label).isInteger(); });
                                });
    if(selector.isInteger() && selector.integer().hasUnknown() && !exact)
      fail(
        at,
        "runtime package case with an X/Z selector requires constant labels");

    // Every item reads the same entry bindings. The RTL builder merges the
    // ordinary result writes using the existing case and branch semantics.
    const auto entered = procedural_locals;
    auto lower_branch = [&](const Statement &branch)
    {
      if(entered.empty())
        return stmt(branch);
      for(auto &entry : procedural_locals)
        entry.second.case_read_only = true;
      auto lowered = stmt(branch);
      procedural_locals = entered;
      return lowered;
    };

    // A unique qualifier adds no match or completeness assumption.
    auto operand = expr(c.expr);
    r = item(
      casez ? ID_verilog_casez : ID_verilog_case,
      {exact ? exprt{true_exprt{}} : std::move(operand)});
    for(auto &group : c.items)
    {
      auto labels = item(ID_empty_string);
      for(auto *x : group.expressions)
      {
        if(exact)
        {
          // Check the original expression before replacing a known match.
          (void)expr(*x);
          const auto label = eval_known(*x);
          labels.operands().push_back(
            exactlyEqual(selector.integer(), label.integer())
              ? exprt{true_exprt{}}
              : exprt{false_exprt{}});
          continue;
        }
        if(package_case)
        {
          const auto label = eval_known(*x);
          if(label.isInteger() && label.integer().hasUnknown())
          {
            if(selector.isInteger() || !c.expr.type->isFourState())
              continue; // X/Z never matches a known binary value in plain case.
            fail(
              x->sourceRange.start(),
              "runtime package case cannot preserve an X/Z label with a "
              "variable four-state selector");
          }
        }
        if(!casez)
        {
          labels.operands().push_back(expr(*x));
          continue;
        }
        reject_checked_call(*x);
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
      r.operands().push_back(
        item(ID_case_item, {std::move(labels), lower_branch(*group.stmt)}));
    }
    if(c.defaultCase)
      r.operands().push_back(
        item(ID_case_item, {item(ID_default), lower_branch(*c.defaultCase)}));
    auto add_check = [&](const irep_idt &key, const char *stem)
    {
      const auto site = sm.getFullyExpandedLoc(at);
      const auto where = loc(site);
      const auto prefix = name(*scope) + ".";
      const auto name = std::string(stem) + "_" + id2string(where.get_line()) +
                        "_" + id2string(where.get_column());
      auto base = name;
      for(unsigned occurrence = 2; object.symbols.lookup(prefix + base);
          ++occurrence)
        base = name + "_" + std::to_string(occurrence);
      const auto id = prefix + base;
      r.add(key) = procedural_assertion(site, base, id, true_exprt{});
      object.metadata.add("properties").add(id).set("origin", "case-qualifier");
    };
    if(check_overlap)
      add_check(
        verilog_case_statement_baset::overlap_check, "$case_unique_overlap");
    if(check_coverage)
      add_check(
        verilog_case_statement_baset::coverage_check,
        unique ? "$case_unique_coverage" : "$case_priority_coverage");
    break;
  }
  case StatementKind::Timed:
  {
    if(function)
      fail(at, "unsupported timing in a runtime function");
    if(!expanded_indices.empty())
      fail(at, "unsupported timing in a statically expanded loop");
    auto &ts = s.as<TimedStatement>();
    const auto clock_id = clock(&ts.timing, at);
    auto &ev = ts.timing.as<SignalEventControl>();
    auto edge = clock_edge(ev.expr, clock_id);
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
    if(!expanded_indices.empty() || !procedural_locals.empty())
      require_read_only(test, "unsupported write in a condition");
    const auto constant = eval_known(test);
    if(constant.isInteger() && !constant.integer().hasUnknown())
    {
      if(constant.isTrue())
        return stmt(c.ifTrue, at_initial_entry);
      if(c.ifFalse)
        return stmt(*c.ifFalse, at_initial_entry);
      r = item(ID_skip);
      break;
    }
    auto condition = boolean(evaluated_expr(test));
    const auto entered = procedural_locals;
    auto when_true = stmt(c.ifTrue);
    const auto after_true = std::move(procedural_locals);
    procedural_locals = entered;
    r = item(ID_if, {std::move(condition), std::move(when_true)});
    if(c.ifFalse)
      r.operands().push_back(stmt(*c.ifFalse));
    merge_procedural_locals(entered, after_true);
    break;
  }
  case StatementKind::ExpressionStatement:
  {
    auto &e = s.as<ExpressionStatement>().expr;
    if(const auto *call = e.as_if<CallExpression>())
    {
      if(in_package_function())
        return function_diagnostic_statement(*call);
      return diagnostic_statement(*call);
    }
    if(e.kind == ExpressionKind::UnaryOp)
    {
      auto &u = e.as<UnaryExpression>();
      if(
        !function && u.op == UnaryOperator::Postincrement &&
        expanded_indices.empty() && procedural_locals.empty() &&
        !locals.count(u.operand().getSymbolReference()))
      {
        r = item(ID_postincrement, {lhs(u.operand())});
        break;
      }
    }
    if(e.kind != ExpressionKind::Assignment)
      fail(at, "expected assignment");
    auto &a = e.as<AssignmentExpression>();
    const auto *target = a.left().getSymbolReference();
    if(
      function && (!is_function_variable(target) || a.isNonBlocking() ||
                   a.timingControl || a.op))
      fail(at, "requires a direct blocking write to a runtime function local");
    if(locals.count(target) || expanded_indices.count(target))
      fail(at, "write to a statically expanded loop index");
    if(auto entry = procedural_locals.find(target);
       entry != procedural_locals.end())
    {
      auto &local = entry->second;
      if(local.case_read_only)
        fail(
          a.left().sourceRange.start(),
          "case branch writes an active procedural local: " +
            std::string(target->name));
      const bool packed_or =
        in_always_comb && a.op == BinaryOperator::BinaryOr &&
        is_automatic_packed_struct(target->as<VariableSymbol>());
      if(
        a.left().kind != ExpressionKind::NamedValue || a.isNonBlocking() ||
        (a.op && !packed_or) || a.timingControl)
        fail(
          at,
          local.require_full_assignment
            ? "unsupported assignment to a combinational local"
            : "unsupported assignment to an automatic local");
      // A compound assignment reads the value from this evaluation before
      // writing the result. Let the RTL builder forward the latest write.
      if(packed_or && !local.assigned)
        fail(
          a.left().sourceRange.start(),
          "combinational local read before assignment: " +
            std::string(target->name));
      auto rhs = packed_or ? expr(a.right(), &local.ref)
                           : evaluated_expr(a.right());
      local.constant = packed_or ? ConstantValue{} : eval_known(a.right());
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
      return procedural_locals.count(symbol) || expanded_indices.count(symbol) ||
             locals.count(symbol);
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
    const bool gated_write = in_gated_async && a.isNonBlocking();
    if(gated_write && a.timingControl)
      fail(
        at, "clock gate contract requires undelayed nonblocking assignments");
    if(!gated_write)
    {
      if(
        auto write = a.isNonBlocking() ? dynamic_packed_write(a)
                                       : dynamic_packed_bank_write(a))
      {
        r = std::move(*write);
        break;
      }
    }
    auto target_expr = gated_write ? gated_async_lhs(a.left()) : lhs(a.left());
    // The typed RHS reads this target in the current procedural state.
    auto rhs = a.op ? expr(a.right(), &target_expr) : evaluated_expr(a.right());
    r = item(
      a.isNonBlocking() ? ID_verilog_non_blocking_assign
                        : ID_verilog_blocking_assign,
      {std::move(target_expr), std::move(rhs)});
    break;
  }
  default:
    fail(at, "unsupported statement " + std::string(toString(s.kind)));
  }
  r.add_source_location() = loc(at);
  return r;
}
} // namespace vv
