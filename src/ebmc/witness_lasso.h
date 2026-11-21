// Bounded certification of an infinite witness for an engine verdict.
#ifndef CPROVER_EBMC_WITNESS_LASSO_H
#define CPROVER_EBMC_WITNESS_LASSO_H

#include "property_checker.h"

void certify_witnesses(
  const cmdlinet &,
  const transition_systemt &,
  property_checker_resultt &,
  std::size_t max_transitions,
  message_handlert &);

#endif
