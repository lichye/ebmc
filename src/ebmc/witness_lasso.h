#ifndef CPROVER_EBMC_WITNESS_LASSO_H
#define CPROVER_EBMC_WITNESS_LASSO_H

#include <util/cmdline.h>

/// Validate the policy before any unwinding (and for direct library callers).
std::optional<std::size_t> witness_lasso_budget(
  const cmdlinet &, std::optional<std::size_t> bmc_bound);

#endif
