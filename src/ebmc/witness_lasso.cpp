#include "witness_lasso.h"

#include <util/arith_tools.h>
#include <util/expr_util.h>

#include <trans-word-level/instantiate_word_level.h>
#include <trans-word-level/trans_trace_word_level.h>
#include <trans-word-level/unwind.h>

#include "ebmc_solver_factory.h"

#include <set>

namespace
{
bool invariant(const exprt &expr)
{
  return (expr.id() == ID_AG || expr.id() == ID_G ||
          expr.id() == ID_sva_always) &&
         !has_temporal_operator(to_unary_expr(expr).op());
}

// Match values on both sides of the repeated frame. This includes the
// assertion and assumption monitors, inputs appearing under next(), and
// implicit per-frame choices which need not have a symbol-table entry.
void collect_boundary(const exprt &expr, std::set<exprt> &boundary)
{
  expr.visit_pre(
    [&](const exprt &sub)
    {
      if(sub.id() == ID_symbol || sub.id() == ID_next_symbol)
        boundary.emplace(symbol_exprt{sub.get(ID_identifier), sub.type()});
      else if(sub.id() == ID_nondet_symbol)
        boundary.insert(sub);
    });
}

// A deterministic monitor assigns next(state) with a priority mux over its
// current state. SMT define-fun expansion of large muxes across time frames
// is costly in CVC5. Equalities to distinct integer state numbers are mutually
// exclusive, so their implications plus the default case are equivalent.
void split_state_muxes(exprt &trans)
{
  if(trans.id() == ID_and)
  {
    for(auto &op : trans.operands())
      split_state_muxes(op);
    return;
  }
  if(trans.id() != ID_equal)
    return;

  const auto &assignment = to_equal_expr(trans);
  const auto &next = assignment.lhs();
  if(next.id() != ID_next_symbol || next.type().id() != ID_range)
    return;

  const auto state = symbol_exprt{next.get(ID_identifier), next.type()};
  const exprt *rhs = &assignment.rhs();
  exprt::operandst guards, constraints;
  std::set<mp_integer> seen;
  while(rhs->id() == ID_if)
  {
    const auto &mux = to_if_expr(*rhs);
    const auto &guard = mux.cond();
    if(guard.id() != ID_equal)
      break;
    const auto &condition = to_equal_expr(guard);
    if(condition.lhs() != state || condition.rhs().type() != state.type())
      break;
    auto value = numeric_cast<mp_integer>(condition.rhs());
    if(
      !value || !to_integer_range_type(state.type()).includes(*value) ||
      !seen.insert(*value).second)
      break;

    guards.push_back(guard);
    constraints.push_back(
      implies_exprt{guard, equal_exprt{next, mux.true_case()}});
    rhs = &mux.false_case();
  }
  if(guards.size() > 1)
  {
    constraints.push_back(
      implies_exprt{not_exprt{disjunction(guards)}, equal_exprt{next, *rhs}});
    trans = conjunction(constraints);
  }
}

void certify(
  const transition_systemt &system,
  const transt &trans,
  const exprt::operandst &assumptions,
  property_checker_resultt::propertyt &property,
  std::size_t end,
  const ebmc_solver_factoryt &factory,
  message_handlert &messages)
{
  messaget message{messages};
  message.status() << property.name << ": certifying witness loop within "
                   << end << " transitions" << messaget::eom;
  const namespacet ns{system.symbol_table};
  auto backend = factory(ns, messages);
  auto &solver = backend.decision_procedure();
  const auto frames = end + 1;
  unwind(trans, messages, solver, frames, ns);

  std::set<exprt> boundary;
  for(const auto &reg : system.state_variables())
    boundary.insert(reg);
  for(const auto &input : system.inputs())
    boundary.insert(input);
  collect_boundary(system.trans_expr, boundary);
  for(const auto &assumption : assumptions)
  {
    collect_boundary(assumption, boundary);
    for(std::size_t t = 0; t < frames; ++t)
      solver.set_to_true(instantiate(assumption, t, frames));
  }

  // Search afresh for the same violation. The primary engine may have put
  // the bad frame at the very end of its prefix.
  const auto &predicate = to_unary_expr(property.normalized_expr).op();
  collect_boundary(predicate, boundary);
  exprt::operandst predicates;
  for(std::size_t t = 0; t < end; ++t)
    predicates.push_back(solver.handle(instantiate(predicate, t, frames)));
  solver.set_to_false(conjunction(predicates));
  predicates.push_back(solver.handle(instantiate(predicate, end, frames)));

  // A shorter lasso can be unrolled around its loop to end at this frame.
  // Only one loop selector per earlier frame is needed.
  exprt::operandst loops;
  for(std::size_t start = 0; start < end; ++start)
  {
    exprt::operandst equalities;
    for(const auto &variable : boundary)
      equalities.emplace_back(equal_exprt{
        instantiate(variable, start, frames),
        instantiate(variable, end, frames)});
    loops.push_back(solver.handle(conjunction(equalities)));
  }
  solver.set_to_true(disjunction(loops));

  switch(solver())
  {
  case decision_proceduret::resultt::D_UNSATISFIABLE:
    message.status() << property.name << ": no witness loop within " << end
                     << " transitions" << messaget::eom;
    property.inconclusive();
    return;
  case decision_proceduret::resultt::D_ERROR:
    property.failure("solver error while certifying witness loop");
    return;
  case decision_proceduret::resultt::D_SATISFIABLE:
    break;
  }

  for(std::size_t start = 0; start < end; ++start)
    if(solver.get(loops[start]).is_true())
    {
      auto trace = compute_trans_trace(
        predicates, solver, frames, ns, system.main_symbol->name);
      auto bad = trace.get_min_failing_timeframe();
      if(bad && *bad < end)
      {
        trace.loop_start = start;
        property.witness_trace = std::move(trace);
        message.status() << property.name << ": witness loop " << start
                         << " -> " << end << " (equal endpoints), bad frame "
                         << *bad << messaget::eom;
        return;
      }
      break;
    }

  property.failure("solver did not return the witness loop or bad frame");
}
} // namespace

void certify_witnesses(
  const cmdlinet &cmdline,
  const transition_systemt &system,
  property_checker_resultt &result,
  std::size_t max_transitions,
  message_handlert &messages)
{
  exprt::operandst assumptions;
  bool supported_assumptions = true;
  for(const auto &p : result.properties)
    if(!p.is_disabled() && (p.is_assumption() || p.is_assumed()))
    {
      if(p.is_assumed() && invariant(p.normalized_expr))
        assumptions.push_back(to_unary_expr(p.normalized_expr).op());
      else
        supported_assumptions = false;
    }

  // Keep the primary engine's transition system and solver selection intact.
  auto trans = system.trans_expr;
  if(cmdline.isset("cvc5"))
    split_state_muxes(trans.trans());

  for(auto &p : result.properties)
  {
    // An engine can mark an earlier finite refutation inconclusive after
    // encountering an unsupported assumption. Do not export that prefix.
    if(p.is_inconclusive())
      p.witness_trace.reset();
    if(
      p.is_assumption() ||
      !(p.is_exists_path() ? p.is_proved() : p.is_refuted()))
      continue;

    p.witness_trace.reset();
    if(!supported_assumptions || !invariant(p.normalized_expr))
    {
      messaget{messages}.status()
        << p.name << ": witness loop requires invariant monitor predicates"
        << messaget::eom;
      p.inconclusive();
      continue;
    }

    certify(
      system,
      trans,
      assumptions,
      p,
      max_transitions,
      ebmc_solver_factory(cmdline),
      messages);
  }
}
