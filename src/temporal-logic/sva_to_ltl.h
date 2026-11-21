/*******************************************************************\

Module: SVA to LTL

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#ifndef CPROVER_TEMPORAL_LOGIC_SVA_TO_LTL_H
#define CPROVER_TEMPORAL_LOGIC_SVA_TO_LTL_H

#include <util/expr.h>

class sva_to_ltl_unsupportedt
{
public:
  explicit sva_to_ltl_unsupportedt(exprt __expr) : expr(std::move(__expr))
  {
  }

  exprt expr;
};

/// If possible, this maps an SVA expression to an equivalent LTL
/// expression, or otherwise throws \ref sva_to_ltl_unsupportedt.
exprt SVA_to_LTL(exprt);

/// Convert a finite sequence implication under an abort. The consequent
/// already accounts for aborts from its own starting tick onward.
exprt SVA_abortable_implication_to_LTL(
  const exprt &implication,
  const exprt &abort,
  const exprt &abortable_consequent);

#endif
