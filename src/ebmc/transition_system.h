/*******************************************************************\

Module: Transition Systems for EBMC

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#ifndef CPROVER_EBMC_TRANSITION_SYSTEM_H
#define CPROVER_EBMC_TRANSITION_SYSTEM_H

#include <util/mathematical_expr.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>

class transition_systemt
{
public:
  transition_systemt()
    : trans_expr{ID_trans, true_exprt(), true_exprt(), true_exprt(), typet()}
  {
  }

  symbol_tablet symbol_table;
  const symbolt *main_symbol;
  transt trans_expr; // transition system expression

  // A complete Spot automaton always has a successor for every letter from
  // every automaton state reachable from its designated initial states.
  // Keep the actual factors so a transformed relation can't inherit the proof.
  struct complete_monitort
  {
    exprt initial;
    exprt transition;
    irep_idt state;
  };
  std::vector<complete_monitort> complete_monitors;

  void output(std::ostream &) const;

  std::vector<symbol_exprt> state_variables() const;
  std::vector<symbol_exprt> inputs() const;
};

#endif // CPROVER_EBMC_TRANSITION_SYSTEM_H
