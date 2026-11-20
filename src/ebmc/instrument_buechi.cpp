/*******************************************************************\

Module: Buechi Automaton Instrumentation

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#include "instrument_buechi.h"

#include "ebmc_error.h"

#include <util/format_expr.h>

#include <temporal-logic/ctl.h>
#include <temporal-logic/ltl_to_buechi.h>
#include <temporal-logic/temporal_logic.h>

void instrument_buechi(
  transition_systemt &transition_system,
  ebmc_propertiest &properties,
  bool functional_assertions,
  message_handlert &message_handler)
{
  for(auto &property : properties.properties)
  {
    const bool assumed = property.is_assumed();
    if(!property.is_unknown() && !assumed)
      continue;

    const auto &formula = property.normalized_expr;
    if(assumed &&
       (formula.id() == ID_sva_always || formula.id() == ID_G ||
        formula.id() == ID_AG) &&
       !has_temporal_operator(to_unary_expr(formula).op()))
      continue; // already an invariant assumption

    // This is for LTL and some fragment of SVA only.
    if(!is_LTL(property.normalized_expr) && !is_SVA(property.normalized_expr))
    {
      if(assumed)
        throw ebmc_errort{} << "unsupported Buechi assumption " << property.name;
      property.unsupported("not convertible to Buechi");
      continue;
    }

    messaget message(message_handler);
    message.debug() << "Translating " << property.description << " to Buechi"
                    << messaget::eom;

    try
    {
      // make the automaton for the negation of the property
      auto buechi = ltl_to_buechi(
        not_exprt{formula}, assumed || functional_assertions, message_handler);

      // An existential choice of monitor run could hide a bad prefix. The
      // HOA must certify determinism and every accepting cycle must already
      // be accounted for by an irrevocable error state.
      if(assumed && (!buechi.deterministic || !buechi.liveness_signal.is_false()))
        throw ebmc_errort{} << "unsupported Buechi assumption " << property.name
                            << ": expected a deterministic safety monitor";

      // make a fresh symbol for the state of the automaton
      namespacet ns(transition_system.symbol_table);
      auto property_symbol = ns.lookup(property.identifier);

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
