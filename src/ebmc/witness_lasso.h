#ifndef CPROVER_EBMC_WITNESS_LASSO_H
#define CPROVER_EBMC_WITNESS_LASSO_H

#include <util/cmdline.h>

#include "ebmc_properties.h"

#include <functional>

class decision_proceduret;
class property_checker_resultt;
class transition_systemt;
class transt;

/// Borrowed certification data, valid only during the synchronous callback.
struct witness_lasso_viewt
{
  const transition_systemt &system;
  const transt &transition;
  const std::vector<const ebmc_propertiest::propertyt *> &assumptions;
  const ebmc_propertiest::propertyt &property;
  const decision_proceduret &solver;
  std::size_t frames;
  std::size_t loop_start;
  std::size_t bad_frame;
};

/// Observe an accepted lasso before its private model and solver are destroyed.
/// The observer may read the solver model. It does not change the query.
using witness_lasso_observert =
  std::function<void(const witness_lasso_viewt &)>;

/// Validate the policy before any unwinding (and for direct library callers).
std::optional<std::size_t>
witness_lasso_budget(const cmdlinet &, std::optional<std::size_t> bmc_bound);

/// Versions for linked clients that opt into observing exact lasso models.
property_checker_resultt property_checker(
  const cmdlinet &,
  transition_systemt &,
  ebmc_propertiest &,
  message_handlert &,
  const witness_lasso_observert &);

void certify_property_witnesses(
  const cmdlinet &,
  const transition_systemt &,
  property_checker_resultt &,
  std::optional<std::size_t> bmc_bound,
  message_handlert &,
  const witness_lasso_observert &);

#endif
