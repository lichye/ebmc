#include "witness_lasso.h"

#include <util/arith_tools.h>
#include <util/expr_util.h>

#include <temporal-logic/nnf.h>
#include <trans-word-level/instantiate_word_level.h>
#include <trans-word-level/lasso.h>
#include <trans-word-level/property.h>
#include <trans-word-level/trans_trace_word_level.h>
#include <trans-word-level/unwind.h>
#include <verilog/sva_expr.h>

#include "ebmc_error.h"
#include "ebmc_solver_factory.h"
#include "instrument_buechi.h"
#include "instrument_past.h"
#include "property_checker.h"

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

// In native BMC a false obligation in this fragment is final on the finite
// prefix. Assertions include pending sequence matches, so a failed positive
// sequence has no possible pending match. Covers exclude pending matches, so
// falsifying the negation of a sequence requires an actual completed match.
bool definitive_prefix(const exprt &nnf, bool allow_pending)
{
  if(!has_temporal_operator(nnf))
    return true;
  const auto id = nnf.id();
  if(id == ID_AG || id == ID_G || id == ID_sva_always)
    return definitive_prefix(to_unary_expr(nnf).op(), allow_pending);
  if(
    id == ID_sva_overlapped_implication ||
    id == ID_sva_non_overlapped_implication)
    return definitive_prefix(to_binary_expr(nnf).rhs(), allow_pending);
  if(
    id == ID_X || id == ID_AX || id == ID_sva_s_nexttime ||
    id == ID_sva_nexttime)
    return definitive_prefix(to_unary_expr(nnf).op(), allow_pending);
  if(
    id == ID_sva_ranged_always || id == ID_sva_s_always ||
    id == ID_sva_eventually || id == ID_sva_ranged_s_eventually)
    return definitive_prefix(nnf.operands().back(), allow_pending);
  if(
    id == ID_sva_accept_on || id == ID_sva_sync_accept_on ||
    id == ID_sva_reject_on || id == ID_sva_sync_reject_on)
    return definitive_prefix(to_sva_abort_expr(nnf).property(), allow_pending);
  if(
    id == ID_sva_overlapped_followed_by ||
    id == ID_sva_nonoverlapped_followed_by)
  {
    const auto &lhs = to_binary_expr(nnf).lhs();
    // With a single-cycle antecedent all its candidate matches are known.
    // A constant zero repetition has only an empty match.
    const bool empty =
      lhs.id() == ID_sva_sequence_repetition_star &&
      lhs.operands().size() == 3 && lhs.operands()[1].is_zero() &&
      (lhs.operands()[2].is_nil() || lhs.operands()[2].is_zero());
    return (!has_temporal_operator(lhs) ||
            (lhs.id() == ID_sva_boolean &&
             !has_temporal_operator(to_unary_expr(lhs).op())) ||
            empty) &&
           definitive_prefix(to_binary_expr(nnf).rhs(), allow_pending);
  }
  if(id == ID_if && !has_temporal_operator(to_if_expr(nnf).cond()))
    return definitive_prefix(to_if_expr(nnf).true_case(), allow_pending) &&
           definitive_prefix(to_if_expr(nnf).false_case(), allow_pending);
  if(id == ID_or || id == ID_and)
    return std::all_of(
      nnf.operands().begin(),
      nnf.operands().end(),
      [&](const exprt &op) { return definitive_prefix(op, allow_pending); });
  const bool negated = id == ID_not;
  const auto seq = (negated ? to_unary_expr(nnf).op() : nnf).id();
  return (negated || allow_pending) &&
         (seq == ID_sva_implicit_strong || seq == ID_sva_implicit_weak ||
          seq == ID_sva_strong || seq == ID_sva_weak);
}

// Sufficient syntactic totality criterion. A freely chosen next value is
// allowed for an unassigned latch; written latches are distinct and each RHS
// is a total current-frame bit-vector function. Initial conditions do not
// constrain successors. Unrecognised operators or state constraints fail closed.
bool total_transition(const transition_systemt &system)
{
  const auto &tr = system.trans_expr;
  // A complete monitor can be composed independently of the design and the
  // other monitors. Only use its construction proof while both exact factors
  // remain present. No other constraint may write its private next state.
  std::map<exprt, irep_idt> complete;
  for(const auto &monitor : system.complete_monitors)
  {
    std::vector<const exprt *> conjuncts{&tr.init()};
    while(!conjuncts.empty())
    {
      const auto &c = *conjuncts.back();
      conjuncts.pop_back();
      if(c == monitor.initial)
      {
        complete.emplace(monitor.transition, monitor.state);
        break;
      }
      if(c.id() == ID_and)
        for(const auto &op : c.operands())
          conjuncts.push_back(&op);
    }
  }
  std::set<irep_idt> assigned;
  const auto all_conjuncts =
    [&](const exprt &root, const auto &check, bool transition = false)
  {
    std::vector<const exprt *> pending{&root};
    while(!pending.empty())
    {
      auto &expr = *pending.back();
      pending.pop_back();
      if(transition && complete.count(expr))
      {
        if(!assigned.insert(complete.at(expr)).second)
          return false;
      }
      else if(expr.id() == ID_and)
        for(const auto &op : expr.operands())
          pending.push_back(&op);
      else if(!expr.is_true() && !check(expr))
        return false;
    }
    return true;
  };
  const auto total_value = [](const exprt &rhs)
  {
    // Copying a value or assigning a constant of the destination's exact
    // type is total, including arrays, floating point and aggregate state.
    if(rhs.id() == ID_symbol || rhs.is_constant())
      return true;
    bool total = true;
    rhs.visit_pre(
      [&](const exprt &e)
      {
        const auto t = e.type().id(), id = e.id();
        if(e.is_constant())
          return;
        if(
          t != ID_bool && t != ID_signedbv && t != ID_unsignedbv &&
          t != ID_floatbv)
          total = false;
        else if(
          id != ID_constant && id != ID_symbol && id != ID_nondet_symbol &&
          id != ID_not && id != ID_and && id != ID_or && id != ID_xor &&
          id != ID_implies && id != ID_equal && id != ID_notequal &&
          id != ID_floatbv_plus && id != ID_bitnot && id != ID_bitand &&
          id != ID_bitor && id != ID_bitxor && id != ID_plus &&
          id != ID_minus && id != ID_mult && id != ID_unary_minus &&
          id != ID_extractbits && id != ID_extractbit && id != ID_shl &&
          id != ID_lshr && id != ID_ashr && id != ID_replication &&
          !(id == ID_constraint_select_one && !e.operands().empty()) &&
          !(id == ID_div && e.operands().size() == 2 &&
            to_binary_expr(e).rhs().is_constant() &&
            !to_binary_expr(e).rhs().is_zero()) &&
          id != ID_typecast && id != ID_if && id != ID_lt && id != ID_le &&
          id != ID_gt && id != ID_ge && id != ID_concatenation)
          total = false;
      });
    return total;
  };
  std::map<irep_idt, const exprt *> wires;
  if(!all_conjuncts(
       tr.invar(),
       [&](const exprt &clause)
       {
         if(clause.id() != ID_equal)
           return false;
         const auto &eq = to_equal_expr(clause);
         if(eq.lhs().type() != eq.rhs().type())
           return false;
         const auto define = [&](const exprt &lhs, const exprt &rhs)
         {
           if(lhs.id() != ID_symbol || !total_value(rhs))
             return false;
           auto id = lhs.get(ID_identifier);
           auto sym = system.symbol_table.lookup(id);
           return sym && !sym->is_state_var && wires.emplace(id, &rhs).second;
         };
         return define(eq.lhs(), eq.rhs()) || define(eq.rhs(), eq.lhs());
       }))
    return false;
  // Acyclic wire definitions give a valuation for every current state/input.
  std::set<irep_idt> active, done;
  std::function<bool(irep_idt)> acyclic = [&](irep_idt id)
  {
    if(done.count(id))
      return true;
    if(!active.insert(id).second)
      return false;
    bool ok = true;
    wires.at(id)->visit_pre(
      [&](const exprt &e)
      {
        if(ok && e.id() == ID_symbol && wires.count(e.get(ID_identifier)))
          ok = acyclic(e.get(ID_identifier));
      });
    active.erase(id);
    done.insert(id);
    return ok;
  };
  for(const auto &wire : wires)
    if(!acyclic(wire.first))
      return false;
  return all_conjuncts(
    tr.trans(),
    [&](const exprt &clause)
    {
      if(clause.id() != ID_equal)
        return false;
      const auto &eq = to_equal_expr(clause);
      if(eq.lhs().id() != ID_next_symbol || eq.lhs().type() != eq.rhs().type())
        return false;
      auto id = eq.lhs().get(ID_identifier);
      auto sym = system.symbol_table.lookup(id);
      const bool stutter =
        eq.rhs().id() == ID_symbol && eq.rhs().get(ID_identifier) == id;
      return sym && sym->is_state_var && assigned.insert(id).second &&
             (stutter || total_value(eq.rhs()));
    },
    true);
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
  const std::vector<const ebmc_propertiest::propertyt *> &assumptions,
  property_checker_resultt::propertyt &property,
  std::size_t end,
  std::optional<std::size_t> bmc_bound,
  const ebmc_solver_factoryt &factory,
  message_handlert &messages,
  const witness_lasso_observert &observe_lasso)
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
      message.status()
        << property.name
        << ": original BMC horizon or property cannot be certified"
        << messaget::eom;
      property.inconclusive();
      return;
    }
    obligations = ::property(
      property.normalized_expr,
      !property.is_exists_path(),
      messages,
      *bmc_bound + 1);
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
  for(const auto *assumption : assumptions)
  {
    const auto &predicate = to_unary_expr(assumption->normalized_expr).op();
    collect_boundary(predicate, boundary);
    for(std::size_t t = 0; t < frames; ++t)
      solver.set_to_true(instantiate(predicate, t, frames));
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
    {
      // A free Boolean selector has a scalar model value even when an SMT
      // solver abbreviates a large conjunction using a shared expression.
      auto selected = symbol_exprt{
        "ebmc::witness_loop::" + std::to_string(start), bool_typet{}};
      solver.set_to_true(implies_exprt{selected, equal});
      loops.push_back(selected);
    }
  }
  if(native_loop)
  {
    for(std::size_t i = 1; i < end; ++i)
      for(std::size_t k = 0; k < i; ++k)
        solver.set_to_false(lasso_symbol(k, i));
    exprt preceding = false_exprt();
    for(const auto &loop : loops)
    {
      solver.set_to_false(and_exprt{preceding, loop});
      preceding = solver.handle(or_exprt{preceding, loop});
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
        if(observe_lasso)
          observe_lasso(
            {system,
             trans,
             assumptions,
             property,
             solver,
             frames,
             start,
             *bad});
        trace.loop_start = start;
        property.witness_certificate =
          property_checker_resultt::propertyt::witness_certificatet::LASSO;
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

std::optional<std::size_t>
witness_lasso_budget(const cmdlinet &cmdline, std::optional<std::size_t> depth)
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
    const auto &arg = cmdline.get_value("witness-lasso");
    std::size_t n;
    const auto p = std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if(
      p.ec != std::errc{} || p.ptr != arg.data() + arg.size() || !n ||
      n == std::numeric_limits<std::size_t>::max())
      throw ebmc_errort{}
        << "--witness-lasso requires a positive transition bound";
    return n;
  }
  const auto base = depth.value_or(0);
  if(base >= std::numeric_limits<std::size_t>::max() - 48)
    throw ebmc_errort{} << "default witness budget overflows";
  return base + 48;
}

void certify_property_witnesses(
  const cmdlinet &cmdline,
  const transition_systemt &original_system,
  property_checker_resultt &result,
  std::optional<std::size_t> bmc_bound,
  message_handlert &messages)
{
  certify_property_witnesses(
    cmdline, original_system, result, bmc_bound, messages, {});
}

void certify_property_witnesses(
  const cmdlinet &cmdline,
  const transition_systemt &original_system,
  property_checker_resultt &result,
  std::optional<std::size_t> bmc_bound,
  message_handlert &messages,
  const witness_lasso_observert &observe_lasso)
{
  const auto budget = witness_lasso_budget(cmdline, bmc_bound);
  if(
    !budget ||
    result.status != property_checker_resultt::statust::VERIFICATION_RESULT)
    return;
  using certificate = property_checker_resultt::propertyt::witness_certificatet;
  const bool no_assumptions = std::none_of(
    result.properties.begin(),
    result.properties.end(),
    [](const auto &p)
    { return !p.is_disabled() && (p.is_assumption() || p.is_assumed()); });
  const bool total = no_assumptions && total_transition(original_system);
  auto solver_options = cmdline;
  for(const auto option : {"outfile", "show-formula", "smt2"})
    if(solver_options.isset(option))
      solver_options.set(option, false);
  bool need_witness = false;
  for(auto &p : result.properties)
  {
    p.witness_certificate = certificate::NONE;
    if(p.is_inconclusive() || p.is_unsupported() || p.is_failure())
      p.witness_trace.reset();
    if(
      !p.is_assumption() &&
      (p.is_exists_path() ? p.is_proved() : p.is_refuted()))
    {
      if(
        no_assumptions && !bmc_bound && !cmdline.isset("witness-lasso") &&
        !cmdline.isset("coi") &&
        (p.proof_via == "BDD CTL, total relation" ||
         (total && p.proof_via == "BDD CTL")))
      {
        p.witness_certificate = certificate::TOTAL_BDD;
        messaget{messages}.status()
          << p.name << ": exact BDD verdict on a total transition system"
          << messaget::eom;
        continue;
      }
      auto bad = p.witness_trace ? p.witness_trace->get_min_failing_timeframe()
                                 : std::optional<std::size_t>{};
      if(
        total && !cmdline.isset("witness-lasso") &&
        (invariant(p.normalized_expr) ||
         !has_temporal_operator(p.normalized_expr) ||
         definitive_prefix(
           property_nnf(p.normalized_expr), !p.is_exists_path())))
      {
        // A COI witness is a projection. Establish a reachable full-model
        // bad prefix before relying on totality; do the same for traceless IC3.
        if(
          cmdline.isset("coi") || !bad ||
          (!bmc_bound && !invariant(p.normalized_expr)))
        {
          const auto depth = bmc_bound.value_or(std::min(
            *budget, bad.value_or(p.witness_depth_hint.value_or(*budget))));
          const namespacet ns{original_system.symbol_table};
          auto backend = ebmc_solver_factory(solver_options)(ns, messages);
          auto &solver = backend.decision_procedure();
          unwind(original_system.trans_expr, messages, solver, depth + 1, ns);
          auto handles = ::property(
            p.normalized_expr, !p.is_exists_path(), messages, depth + 1);
          for(auto &h : handles)
            h = solver.handle(h);
          solver.set_to_false(conjunction(handles));
          auto verdict = solver();
          if(verdict == decision_proceduret::resultt::D_ERROR)
          {
            p.witness_trace.reset();
            p.failure("solver error checking full-model prefix");
            continue;
          }
          if(verdict == decision_proceduret::resultt::D_SATISFIABLE)
            p.witness_trace = compute_trans_trace(
              handles,
              solver,
              depth + 1,
              ns,
              original_system.main_symbol->name);
          else
            p.witness_trace.reset();
          bad = p.witness_trace ? p.witness_trace->get_min_failing_timeframe()
                                : std::optional<std::size_t>{};
        }
        if(!bad || (bmc_bound && *bad > *bmc_bound))
        {
          p.witness_trace.reset();
          p.inconclusive();
          continue;
        }
        p.witness_trace->loop_start.reset();
        p.witness_trace->total_extension = true;
        p.witness_certificate = certificate::TOTAL_PREFIX;
        messaget{messages}.status()
          << p.name << ": finite bad frame " << *bad
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
  system.main_symbol =
    &system.symbol_table.lookup_ref(original_system.main_symbol->name);
  ebmc_propertiest batch;
  batch.properties = result.properties;
  // Model past in active assumptions and goals still requiring a certificate.
  // A discharged sibling's private history is not part of the original machine.
  for(auto &p : batch.properties)
    if(
      !p.is_assumption() && !p.is_assumed() &&
      (p.witness_certificate != certificate::NONE ||
       !(p.is_exists_path() ? p.is_proved() : p.is_refuted())))
      p.normalized_expr = true_exprt();
  bool compiled = true;
  for(const auto &p : batch.properties)
    if(
      !p.is_disabled() && p.is_assumption() && p.normalized_expr.is_true() &&
      has_temporal_operator(to_unary_expr(p.original_expr).op()))
      compiled = false; // fairness erased by a liveness-to-safety transform

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
  std::vector<const ebmc_propertiest::propertyt *> assumptions;
  bool supported_assumptions = compiled;
  for(const auto &p : batch.properties)
    if(!p.is_disabled() && (p.is_assumption() || p.is_assumed()))
    {
      if(p.is_assumed() && invariant(p.normalized_expr))
        assumptions.push_back(&p);
      else
        supported_assumptions = false;
    }

  // Keep the primary engine's transition system and solver selection intact.
  // An engine may ignore an output flag and return a real verdict. The
  // secondary query always needs a solver, never a formula writer.
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

    auto horizon = bmc_bound;
    if(
      !horizon && !invariant(p.normalized_expr) &&
      bmc_supports_property(p.normalized_expr))
    {
      // Checking an unbounded goal on an actual lasso is sound for the
      // path fragment accepted by native BMC. Branching CTL is excluded.
      horizon = p.witness_trace && !p.witness_trace->states.empty()
                  ? std::min(*budget, p.witness_trace->states.size() - 1)
                  : *budget;
    }
    p.witness_trace.reset();
    if(!supported_assumptions || (!horizon && !invariant(p.normalized_expr)))
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
      horizon,
      ebmc_solver_factory(solver_options),
      messages,
      observe_lasso);
  }
  auto source = batch.properties.begin();
  for(auto &p : result.properties)
    p.copy_results_from(*source++);
}
