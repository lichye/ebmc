/*******************************************************************\

Module: Buechi Automaton Instrumentation

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#include "instrument_buechi.h"

#include <util/expr_util.h>
#include <util/format_expr.h>

#include <temporal-logic/ctl.h>
#include <temporal-logic/ltl_to_buechi.h>
#include <temporal-logic/temporal_logic.h>
#include <trans-word-level/next_symbol.h>
#include <verilog/sva_expr.h>

#include "ebmc_error.h"

namespace
{
bool has_local(const exprt &expr)
{
  return has_subexpr(
    expr,
    [](const exprt &sub)
    {
      return sub.id() == ID_sva_local ||
             sub.id() == ID_sva_sequence_match_items;
    });
}

// Follow only mandatory concatenations. Filtering a capture under first_match,
// repetition, or an alternative could change which match wins or bypass it.
void find_capture(exprt &seq, exprt *&capture)
{
  if(seq.id() == ID_sva_sequence_match_items)
  {
    if(capture || seq.get_bool(ID_initial))
      throw ltl_to_buechi_unsupportedt{seq};
    capture = &seq;
  }
  else if(seq.id() == ID_sva_cycle_delay)
  {
    auto &delay = to_sva_cycle_delay_expr(seq);
    if(delay.lhs().is_nil() || delay.is_range() || !delay.from().is_one())
      throw ltl_to_buechi_unsupportedt{seq};
    find_capture(delay.lhs(), capture);
    find_capture(delay.rhs(), capture);
  }
  else if(has_local(seq))
    throw ltl_to_buechi_unsupportedt{seq};
}

// For a universally checked assertion, all matches having captured K must
// satisfy the consequent, for every K. An unconstrained initial K with K'=K
// implements precisely this quantification, even for overlapping starts or
// distinct endpoints on the same trace. No transitions of the design are lost.
auxiliary_symbolt
lower_assertion_capture(exprt &formula, const symbolt &property)
{
  if(formula.id() != ID_sva_always && formula.id() != ID_G)
    throw ltl_to_buechi_unsupportedt{formula};
  auto &implication = to_unary_expr(formula).op();
  if(
    implication.id() != ID_sva_overlapped_implication &&
    implication.id() != ID_sva_non_overlapped_implication)
    throw ltl_to_buechi_unsupportedt{implication};
  auto &rule = to_binary_expr(implication);
  exprt *capture = nullptr;
  find_capture(rule.lhs(), capture);
  if(!capture || has_temporal_operator(rule.rhs()))
    throw ltl_to_buechi_unsupportedt{implication};

  const auto &match = to_binary_expr(*capture);
  if(
    match.lhs().id() != ID_sva_boolean || has_local(match.lhs()) ||
    match.rhs().operands().size() != 1)
    throw ltl_to_buechi_unsupportedt{*capture};
  const auto &assignment = match.rhs().operands().front();
  if(assignment.id() != ID_verilog_blocking_assign)
    throw ltl_to_buechi_unsupportedt{assignment};
  const auto &write = to_binary_expr(assignment);
  const auto &local = write.lhs();
  if(
    local.id() != ID_sva_local ||
    (local.type().id() != ID_bool && local.type().id() != ID_signedbv &&
     local.type().id() != ID_unsignedbv) ||
    write.rhs().type() != local.type() || write.rhs().is_nil() ||
    has_local(write.rhs()))
    throw ltl_to_buechi_unsupportedt{assignment};

  auxiliary_symbolt witness{
    id2string(property.name) + "::buechi_capture", local.type(), property.mode};
  witness.module = property.module;
  witness.location = local.source_location();
  witness.is_state_var = true;
  const auto frozen = witness.symbol_expr();
  rule.rhs().visit_pre(
    [&](exprt &e)
    {
      if(e.id() == ID_sva_local)
      {
        if(
          e.get(ID_identifier) != local.get(ID_identifier) ||
          e.type() != local.type())
          throw ltl_to_buechi_unsupportedt{e};
        e = frozen;
      }
    });
  auto guarded = sva_boolean_exprt{
    and_exprt{
      to_sva_boolean_expr(match.lhs()).op(), equal_exprt{frozen, write.rhs()}},
    capture->type()};
  *capture = std::move(guarded);
  return witness;
}
} // namespace

void instrument_buechi(
  transition_systemt &transition_system,
  ebmc_propertiest &properties,
  bool functional_assertions,
  message_handlert &message_handler,
  bool assumptions_only)
{
  for(auto &property : properties.properties)
  {
    const bool assumed = property.is_assumed();
    if(!assumed && (assumptions_only || !property.is_unknown()))
      continue;

    auto formula = property.normalized_expr;
    const bool uses_local = has_local(formula);
    if(assumed && uses_local)
      throw ebmc_errort{}
        << "unsupported Buechi assumption " << property.name
        << ": assertion locals require a universally checked assertion";

    if(
      assumed &&
      (formula.id() == ID_sva_always || formula.id() == ID_G ||
       formula.id() == ID_AG) &&
      !has_temporal_operator(to_unary_expr(formula).op()))
      continue; // already an invariant assumption

    // This is for LTL and some fragment of SVA only.
    if(!is_LTL(property.normalized_expr) && !is_SVA(property.normalized_expr))
    {
      if(assumed)
        throw ebmc_errort{} << "unsupported Buechi assumption "
                            << property.name;
      property.unsupported("not convertible to Buechi");
      continue;
    }

    messaget message(message_handler);
    message.debug() << "Translating " << property.description << " to Buechi"
                    << messaget::eom;

    try
    {
      namespacet ns(transition_system.symbol_table);
      const auto property_symbol = ns.lookup(property.identifier);
      std::optional<auxiliary_symbolt> capture;
      if(uses_local)
      {
        if(property.original_expr.id() == ID_sva_cover)
          throw ltl_to_buechi_unsupportedt{formula};
        capture = lower_assertion_capture(formula, property_symbol);
      }

      // make the automaton for the negation of the property
      auto buechi = ltl_to_buechi(
        not_exprt{formula}, assumed || functional_assertions, message_handler);

      // An existential choice of monitor run could hide a bad prefix. The
      // HOA must certify determinism and every accepting cycle must already
      // be accounted for by an irrevocable error state.
      if(
        assumed &&
        (!buechi.deterministic || !buechi.liveness_signal.is_false()))
        throw ebmc_errort{} << "unsupported Buechi assumption " << property.name
                            << ": expected a deterministic safety monitor";

      if(capture)
      {
        auto frozen = capture->symbol_expr();
        if(transition_system.symbol_table.add(std::move(*capture)))
          throw ebmc_errort{} << "duplicate Buechi capture symbol "
                              << frozen.identifier();
        transition_system.trans_expr.trans() = and_exprt{
          transition_system.trans_expr.trans(),
          equal_exprt{next_symbol_exprt{frozen}, frozen}};
      }

      // make a fresh symbol for the state of the automaton
      auxiliary_symbolt new_state_symbol{
        id2string(property_symbol.name) + "::buechi_state",
        buechi.state_symbol.type(),
        property_symbol.mode};
      new_state_symbol.is_state_var = true;
      new_state_symbol.module = property_symbol.module;

      buechi.rename_state_symbol(new_state_symbol.symbol_expr());

      // add the new symbol to the symbol table
      transition_system.symbol_table.add(std::move(new_state_symbol));

      // add the automaton to the transition system
      transition_system.trans_expr.init() =
        and_exprt{transition_system.trans_expr.init(), buechi.init};

      transition_system.trans_expr.trans() =
        and_exprt{transition_system.trans_expr.trans(), buechi.trans};

      if(assumed)
      {
        property.normalized_expr = AG_exprt{not_exprt{buechi.error_signal}};
        continue;
      }

      // Replace the normalized property expression
      // by the Buechi acceptance condition.
      exprt::operandst property_conjuncts;

      bool have_liveness = false, have_safety = false;

      if(!buechi.liveness_signal.is_false())
      {
        // Note that we have negated the property,
        // so this is the negation of the Buechi acceptance condition.
        // We generate CTL, which is more broadly supported.
        property_conjuncts.push_back(
          AF_exprt{AG_exprt{not_exprt{buechi.liveness_signal}}});
        have_liveness = true;
      }

      if(!buechi.error_signal.is_true())
      {
        // We generate CTL, which is more broadly supported.
        property_conjuncts.push_back(AG_exprt{not_exprt{buechi.error_signal}});
        have_safety = true;
      }

      if(have_liveness && have_safety)
        message.debug() << "Buechi automaton has liveness and safety components"
                        << messaget::eom;
      else if(have_liveness)
        message.debug() << "Buechi automaton is liveness only" << messaget::eom;
      else if(have_safety)
        message.debug() << "Buechi automaton is safety only" << messaget::eom;

      property.normalized_expr = conjunction(property_conjuncts);

      message.debug() << "New property: " << format(property.normalized_expr)
                      << messaget::eom;
    }
    catch(const ltl_to_buechi_unsupportedt &error)
    {
      if(assumed)
        throw ebmc_errort{} << "unsupported Buechi assumption " << property.name
                            << ": " << error.expr.id();
      property.unsupported(
        error.expr.id_string() + " not convertible to Buechi");
      continue;
    }
  }
}
