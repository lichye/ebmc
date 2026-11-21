#include "witness_lasso.h"
#include "property_checker.h"

#include <util/arith_tools.h>
#include <util/expr_util.h>

#include <trans-word-level/instantiate_word_level.h>
#include <trans-word-level/lasso.h>
#include <trans-word-level/property.h>
#include <trans-word-level/trans_trace_word_level.h>
#include <trans-word-level/unwind.h>

#include "ebmc_solver_factory.h"
#include "ebmc_error.h"
#include "instrument_buechi.h"
#include "instrument_past.h"

#include <charconv>
#include <limits>
#include <set>

namespace
{
bool invariant(const exprt &expr)
{
  return (expr.id() == ID_AG || expr.id() == ID_G ||
          expr.id() == ID_sva_always) &&
         !has_temporal_operator(to_unary_expr(expr).op());
}

// Sufficient syntactic totality criterion. A freely chosen next value is
// allowed for an unassigned latch; written latches are distinct and each RHS
// is a total current-frame bit-vector function. Initial conditions do not
// constrain successors. Unrecognised operators or state constraints fail closed.
bool total_transition(const transition_systemt &system)
{
  const auto &tr = system.trans_expr;
  const auto all_conjuncts = [](const exprt &root, const auto &check) {
    std::vector<const exprt *> pending{&root};
    while(!pending.empty())
    {
      auto &expr = *pending.back();
      pending.pop_back();
      if(expr.id() == ID_and)
        for(const auto &op : expr.operands())
          pending.push_back(&op);
      else if(!expr.is_true() && !check(expr))
        return false;
    }
    return true;
  };
  if(!all_conjuncts(tr.invar(), [](const exprt &) { return false; }))
    return false;
  std::set<irep_idt> assigned;
  return all_conjuncts(tr.trans(), [&](const exprt &clause) {
    if(clause.id() != ID_equal)
      return false;
    const auto &eq = to_equal_expr(clause);
    if(eq.lhs().id() != ID_next_symbol || eq.lhs().type() != eq.rhs().type() ||
       !assigned.insert(eq.lhs().get(ID_identifier)).second)
      return false;
    bool total = true;
    eq.rhs().visit_pre([&](const exprt &e) {
      const auto t=e.type().id(), id=e.id();
      if(t != ID_bool && t != ID_signedbv && t != ID_unsignedbv)
        total = false;
      else if(id != ID_constant && id != ID_symbol && id != ID_nondet_symbol &&
              id != ID_not && id != ID_and && id != ID_or && id != ID_xor &&
              id != ID_implies && id != ID_equal && id != ID_notequal &&
              id != ID_bitnot && id != ID_bitand && id != ID_bitor && id != ID_bitxor &&
              id != ID_plus && id != ID_minus && id != ID_mult && id != ID_unary_minus &&
              id != ID_typecast && id != ID_if && id != ID_lt && id != ID_le &&
              id != ID_gt && id != ID_ge && id != ID_concatenation)
        total = false;
    });
    return total;
  });
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
  const namespacet &trace_ns,
  const exprt::operandst &assumptions,
  property_checker_resultt::propertyt &property,
  std::size_t end,
  std::optional<std::size_t> bmc_bound,
  const ebmc_solver_factoryt &factory,
  message_handlert &messages)
{
  messaget message{messages};
  message.status() << property.name << ": certifying witness loop within "
                   << end << " transitions" << messaget::eom;
  const namespacet ns{system.symbol_table};
  auto backend = factory(ns, messages);
  auto &solver = backend.decision_procedure();
  // Use the original finite obligation and its polarity. In particular its
  // pending matches must never be re-evaluated at the extension horizon.
  exprt::operandst obligations;
  bool native_loop = false;
  if(bmc_bound)
  {
    if(*bmc_bound > end || !bmc_supports_property(property.normalized_expr))
    {
      message.status() << property.name << ": original BMC horizon or property cannot be certified" << messaget::eom;
      property.inconclusive();
      return;
    }
    obligations = ::property(property.normalized_expr, !property.is_exists_path(), messages, *bmc_bound+1);
    native_loop = uses_lasso_symbol(obligations);
    if(native_loop)
      end = *bmc_bound;
  }
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
  collect_boundary(property.normalized_expr, boundary);
  exprt::operandst predicates;
  if(bmc_bound)
  {
    for(const auto &obligation : obligations)
      predicates.push_back(solver.handle(obligation));
  }
  else
  {
    const auto &predicate = to_unary_expr(property.normalized_expr).op();
    for(std::size_t t = 0; t < end; ++t)
      predicates.push_back(solver.handle(instantiate(predicate, t, frames)));
  }
  solver.set_to_false(conjunction(predicates));
  predicates.resize(frames, true_exprt());

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
    auto equal = conjunction(equalities);
    if(native_loop)
    {
      // Select exactly one native lasso and certify that very same back
      // edge, including all history and assumption monitor state.
      auto selected = lasso_symbol(start, end);
      solver.set_to_true(implies_exprt{selected, equal});
      loops.push_back(solver.handle(selected));
    }
    else
      loops.push_back(solver.handle(equal));
  }
  if(native_loop)
  {
    for(std::size_t i = 1; i < end; ++i)
      for(std::size_t k = 0; k < i; ++k)
        solver.set_to_false(lasso_symbol(k,i));
    exprt preceding = false_exprt();
    for(const auto &loop : loops)
    {
      solver.set_to_false(and_exprt{preceding,loop});
      preceding = solver.handle(or_exprt{preceding,loop});
    }
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
        predicates, solver, frames, trace_ns, system.main_symbol->name);
      auto bad = trace.get_min_failing_timeframe();
      if(bad && (bmc_bound ? *bad <= *bmc_bound : *bad < end))
      {
        trace.loop_start = start;
        property.witness_certificate = property_checker_resultt::propertyt::witness_certificatet::LASSO;
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

std::optional<std::size_t> witness_lasso_budget(
  const cmdlinet &cmdline, std::optional<std::size_t> depth)
{
  if(cmdline.isset("finite-prefix"))
  {
    if(cmdline.isset("witness-lasso"))
      throw ebmc_errort{} << "--finite-prefix conflicts with --witness-lasso";
    return {};
  }
  if(depth && *depth == std::numeric_limits<std::size_t>::max())
    throw ebmc_errort{} << "BMC frame count overflows";
  if(cmdline.isset("witness-lasso"))
  {
    const auto &arg=cmdline.get_value("witness-lasso");
    std::size_t n;
    const auto p=std::from_chars(arg.data(),arg.data()+arg.size(),n);
    if(p.ec!=std::errc{} || p.ptr!=arg.data()+arg.size() || !n ||
       n==std::numeric_limits<std::size_t>::max())
      throw ebmc_errort{} << "--witness-lasso requires a positive transition bound";
    return n;
  }
  const auto base=depth.value_or(0);
  if(base >= std::numeric_limits<std::size_t>::max()-48)
    throw ebmc_errort{} << "default witness budget overflows";
  return base+48;
}

void certify_property_witnesses(
  const cmdlinet &cmdline,
  const transition_systemt &original_system,
  property_checker_resultt &result,
  std::optional<std::size_t> bmc_bound,
  message_handlert &messages)
{
  const auto budget=witness_lasso_budget(cmdline,bmc_bound);
  if(!budget || result.status!=property_checker_resultt::statust::VERIFICATION_RESULT)
    return;
  using certificate = property_checker_resultt::propertyt::witness_certificatet;
  const bool no_assumptions = std::none_of(result.properties.begin(),result.properties.end(),
    [](const auto &p) { return !p.is_disabled() && (p.is_assumption() || p.is_assumed()); });
  const bool total = no_assumptions && total_transition(original_system);
  bool need_witness = false;
  for(auto &p : result.properties)
  {
    if(p.is_inconclusive() || p.is_unsupported() || p.is_failure())
      p.witness_trace.reset();
    if(!p.is_assumption() && (p.is_exists_path() ? p.is_proved() : p.is_refuted()))
    {
      if(total && !bmc_bound && p.proof_via == "BDD CTL" && is_CTL(p.normalized_expr))
      {
        p.witness_certificate=certificate::TOTAL_BDD;
        messaget{messages}.status() << p.name << ": exact BDD verdict on a total transition system" << messaget::eom;
        continue;
      }
      auto bad = p.witness_trace ? p.witness_trace->get_min_failing_timeframe() : std::optional<std::size_t>{};
      if(total && !cmdline.isset("witness-lasso") && bad && (!bmc_bound || *bad<=*bmc_bound) &&
         (invariant(p.normalized_expr) || !has_temporal_operator(p.normalized_expr)))
      {
        p.witness_trace->total_extension = true;
        p.witness_certificate=certificate::TOTAL_PREFIX;
        messaget{messages}.status() << p.name << ": finite bad frame " << *bad
                                    << " has an infinite extension by totality" << messaget::eom;
        continue;
      }
      need_witness = true;
    }
  }
  if(!need_witness)
    return;

  // The candidate engine has finished. Instrument the certification model
  // privately so linked callers can reuse their model and property batch.
  auto system = original_system;
  system.main_symbol = &system.symbol_table.lookup_ref(original_system.main_symbol->name);
  ebmc_propertiest batch;
  batch.properties = result.properties;
  bool compiled = true;
  try
  {
    instrument_past(system, batch);
    instrument_buechi(system, batch, true, messages, true);
  }
  catch(const ebmc_errort &error)
  {
    messaget{messages}.warning() << error.what() << messaget::eom;
    compiled = false;
  }
  exprt::operandst assumptions;
  bool supported_assumptions = compiled;
  for(const auto &p : batch.properties)
    if(!p.is_disabled() && (p.is_assumption() || p.is_assumed()))
    {
      if(p.is_assumed() && invariant(p.normalized_expr))
        assumptions.push_back(to_unary_expr(p.normalized_expr).op());
      else
        supported_assumptions = false;
    }

  // Keep the primary engine's transition system and solver selection intact.
  // An engine may ignore an output flag and return a real verdict. The
  // secondary query always needs a solver, never a formula writer.
  auto solver_options = cmdline;
  for(const auto option : {"outfile", "show-formula", "smt2"})
    if(solver_options.isset(option))
      solver_options.set(option, false);
  auto trans = system.trans_expr;
  if(cmdline.isset("cvc5"))
    split_state_muxes(trans.trans());

  for(auto &p : batch.properties)
  {
    // An engine can mark an earlier finite refutation inconclusive after
    // encountering an unsupported assumption. Do not export that prefix.
    if(p.is_inconclusive())
      p.witness_trace.reset();
    if(
      p.witness_certificate != certificate::NONE || p.is_assumption() ||
      !(p.is_exists_path() ? p.is_proved() : p.is_refuted()))
      continue;

    p.witness_trace.reset();
    if(!supported_assumptions || (!bmc_bound && !invariant(p.normalized_expr)))
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
      namespacet{original_system.symbol_table},
      assumptions,
      p,
      *budget,
      bmc_bound,
      ebmc_solver_factory(solver_options),
      messages);
  }
  auto source = batch.properties.begin();
  for(auto &p : result.properties)
    p.copy_results_from(*source++);
}
