#include <slang/analysis/DataFlowAnalysis.h>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/ValuePath.h>
#include <slang/ast/expressions/Operator.h>

#include "lower.h"

namespace vv
{
namespace
{
using bit_ranget = std::pair<uint64_t, uint64_t>;

// Data flow uses the longest static prefix for a dynamic write. For latch
// checking, only a separate unconditional assignment can cover its full width.
void whole_blocking_writes(const Statement &s, std::set<const Symbol *> &writes)
{
  if(const auto *block = s.as_if<BlockStatement>())
  {
    if(block->blockKind == StatementBlockKind::Sequential)
      whole_blocking_writes(block->body, writes);
  }
  else if(const auto *list = s.as_if<StatementList>())
  {
    for(const auto *child : list->list)
      whole_blocking_writes(*child, writes);
  }
  else if(const auto *statement = s.as_if<ExpressionStatement>())
  {
    const auto *assign = statement->expr.as_if<AssignmentExpression>();
    if(
      assign && !assign->op && !assign->isNonBlocking() &&
      !assign->timingControl &&
      assign->left().kind == ExpressionKind::NamedValue)
      writes.insert(assign->left().getSymbolReference());
  }
}

bool subroutine_local(const ValueSymbol &symbol, const SubroutineSymbol &callee)
{
  auto scope = symbol.getParentScope();
  while(scope && scope->asSymbol().kind == SymbolKind::StatementBlock)
    scope = scope->asSymbol().getParentScope();
  return scope == &callee;
}
} // namespace

bool compilert::checked_level_event(
  const ProceduralBlockSymbol &procedure,
  const TimedStatement &timed)
{
  std::vector<const TimingControl *> events;
  if(timed.timing.kind == TimingControlKind::SignalEvent)
    events.push_back(&timed.timing);
  else if(const auto *list = timed.timing.as_if<EventListControl>())
    events.assign(list->events.begin(), list->events.end());
  else
    return false;

  const auto is_level = [](const TimingControl *control)
  {
    const auto *event = control->as_if<SignalEventControl>();
    return event && event->edge == EdgeKind::None;
  };
  if(std::none_of(events.begin(), events.end(), is_level))
    return false;
  const auto at = timed.timing.sourceRange.start();
  if(!std::all_of(events.begin(), events.end(), is_level))
    fail(at, "mixed level and edge sensitivity is unsupported");

  EvalContext context(procedure);
  BumpAllocator storage;
  analysis::SymbolBitMap::allocator_type ranges(storage);
  std::map<const ValueSymbol *, analysis::SymbolBitMap> sensitivity;
  for(const auto *control : events)
  {
    const auto &event = control->as<SignalEventControl>();
    if(event.iffCondition)
      fail(
        control->sourceRange.start(),
        "guarded level sensitivity is unsupported");
    const ValuePath path(event.expr, context);
    if(
      !path.lsp || !path.isFullyStatic() || !event.expr.type->isIntegral() ||
      path.lspBounds.second - path.lspBounds.first + 1 !=
        event.expr.type->getBitWidth())
      fail(
        event.expr.sourceRange.start(),
        "cannot determine level sensitivity for this event expression");
    const auto *symbol = path.rootSymbol();
    if(
      (symbol->kind != SymbolKind::Net &&
       !VariableSymbol::isKind(symbol->kind)) ||
      gated_clock(*path.rootExpr))
      fail(event.expr.sourceRange.start(), "unsupported level sensitivity signal");
    sensitivity[symbol].unionWith(path.lspBounds, {}, ranges);
  }

  std::set<const Symbol *> local, whole_writes;
  whole_blocking_writes(timed.stmt, whole_writes);
  auto check_body = makeVisitor(
    [&](auto &visitor, const BlockStatement &block)
    {
      if(block.blockKind != StatementBlockKind::Sequential)
        fail(
          block.sourceRange.start(),
          "parallel block in a level-sensitive process");
      visitor.visitDefault(block);
    },
    [&](auto &visitor, const VariableDeclStatement &declaration)
    {
      local.insert(&declaration.symbol);
      visitor.visitDefault(declaration);
    },
    [&](auto &visitor, const ForLoopStatement &loop)
    {
      local.insert(loop.loopVars.begin(), loop.loopVars.end());
      visitor.visitDefault(loop);
    },
    [&](auto &visitor, const AssignmentExpression &assign)
    {
      if(assign.timingControl || assign.isNonBlocking())
        fail(
          assign.sourceRange.start(),
          "level-sensitive process requires untimed blocking writes");
      ValuePath::visitPaths(
        assign.left(),
        context,
        [&](const ValuePath &path)
        {
          if(!path.lsp)
            fail(
              assign.left().sourceRange.start(),
              "cannot determine a level-sensitive write target");
          if(!path.isFullyStatic() && !whole_writes.count(path.rootSymbol()))
            fail(
              assign.left().sourceRange.start(),
              "dynamic write in a level-sensitive process needs an unconditional whole assignment");
        },
        /* skipSelectors */ true);
      visitor.visitDefault(assign);
    },
    [&](auto &, const ImmediateAssertionStatement &assertion)
    {
      fail(
        assertion.sourceRange.start(),
        "assertions inside a level-sensitive process are unsupported");
    });
  timed.stmt.visit(check_body);

  analysis::AnalysisContext analysis_context(am);
  analysis::DefaultDFA dfa(analysis_context, procedure, false);
  dfa.run();
  if(dfa.bad || !dfa.endIsReachable)
    fail(at, "cannot analyze level-sensitive process reads");
  const auto controls = dfa.getTimedStatements();
  if(controls.size() != 1 || controls.front() != &timed)
    fail(at, "level-sensitive process requires a single outer event control");
  dfa.visitPartiallyAssigned(
    /* skipAutomatic */ true,
    [&](const Symbol &symbol, const Expression &target)
    {
      fail(
        target.sourceRange.start(),
        "level-sensitive process has an incomplete assignment to " +
          symbol.getHierarchicalPath());
    });

  std::set<std::string> missing;
  bool has_external_reads = false;
  auto check_read = [&](const ValueSymbol &symbol, bit_ranget bits)
  {
    if(
      local.count(&symbol) || symbol.kind == SymbolKind::Parameter ||
      symbol.kind == SymbolKind::EnumValue)
      return;
    has_external_reads = true;
    if(const auto *write = dfa.getLValue(symbol);
       write && write->assigned.find(bits).valid())
      fail(
        at,
        "level-sensitive process reads a value it drives: " +
          symbol.getHierarchicalPath());

    const auto found = sensitivity.find(&symbol);
    if(found != sensitivity.end())
    {
      auto interval = found->second.find(bits);
      if(
        interval.valid() && interval.bounds().first <= bits.first &&
        interval.bounds().second >= bits.second)
        return;
    }
    auto signal = symbol.getHierarchicalPath();
    if(bits.first != 0 || bits.second + 1 != symbol.getType().getSelectableWidth())
      signal += " (bits " + std::to_string(bits.second) + ":" +
                std::to_string(bits.first) + ")";
    missing.insert(std::move(signal));
  };
  for(const auto &[symbol, intervals] : dfa.getRValues())
    for(auto interval = intervals.begin(); interval.valid(); ++interval)
      check_read(*symbol, interval.bounds());

  std::set<const SubroutineSymbol *> visited;
  const auto check_call = [&](auto &&self, const CallExpression &call) -> void
  {
    const auto location = call.sourceRange.start();
    const auto unsupported = [&]()
    {
      fail(
        location,
        "cannot analyze level-sensitive reads for " +
          std::string(call.getSubroutineName()));
    };
    if(call.isSystemCall())
    {
      const auto task = call.getSubroutineName();
      if(
        task != "$bits" && task != "$clog2" && task != "$signed" &&
        task != "$unsigned" && task != "$onehot0")
        unsupported();
      return;
    }
    const auto &callee = *std::get<const SubroutineSymbol *>(call.subroutine);
    if(
      call.thisClass() ||
      callee.flags.has(
        MethodFlags::Virtual | MethodFlags::Pure | MethodFlags::InterfaceExtern |
        MethodFlags::ModportImport | MethodFlags::DPIImport |
        MethodFlags::BuiltIn | MethodFlags::Randomize) ||
      callee.defaultLifetime != VariableLifetime::Automatic ||
      callee.subroutineKind != SubroutineKind::Function || !callee.returnValVar ||
      std::any_of(
        callee.getArguments().begin(),
        callee.getArguments().end(),
        [](const auto *arg) { return arg->direction != ArgumentDirection::In; }))
      unsupported();
    if(!visited.insert(&callee).second)
      return;
    // Instance caching can leave the concrete callee out of the manager's
    // result table. Analyze the specialized body we are about to lower.
    analysis::DefaultDFA body(analysis_context, callee, false);
    body.run();
    if(body.bad || !body.getTimedStatements().empty())
      unsupported();
    for(const auto &write : body.getLValues())
      if(!subroutine_local(*write.symbol, callee))
        unsupported();
    for(const auto &[symbol, intervals] : body.getRValues())
      if(!subroutine_local(*symbol, callee))
        for(auto interval = intervals.begin(); interval.valid(); ++interval)
          check_read(*symbol, interval.bounds());
    for(const auto *nested : body.getCallExpressions())
      self(self, *nested);
  };
  for(const auto *call : dfa.getCallExpressions())
    check_call(check_call, *call);

  if(!has_external_reads)
    fail(at, "level-sensitive process has no external reads");
  if(!missing.empty())
  {
    std::string diagnostic = "level sensitivity is missing";
    for(const auto &signal : missing)
      diagnostic += " " + signal;
    fail(at, diagnostic);
  }
  return true;
}
} // namespace vv
