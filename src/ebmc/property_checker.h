/*******************************************************************\

Module: EBMC Property Checker

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#ifndef CPROVER_EBMC_PROPERTY_CHECKER_H
#define CPROVER_EBMC_PROPERTY_CHECKER_H

#include "ebmc_properties.h"
#include "transition_system.h"

class property_checker_resultt
{
public:
  enum class statust
  {
    VERIFICATION_RESULT, // property status populated
    SUCCESS,             // exit without error, but no property status
    ERROR                // error
  } status;

  explicit property_checker_resultt(statust _status) : status(_status)
  {
  }

  // copy
  explicit property_checker_resultt(const ebmc_propertiest &_properties)
    : status(statust::VERIFICATION_RESULT), properties(_properties.properties)
  {
  }

  static property_checker_resultt error()
  {
    return property_checker_resultt{statust::ERROR};
  }

  static property_checker_resultt success()
  {
    return property_checker_resultt{statust::SUCCESS};
  }

  using propertyt = ebmc_propertiest::propertyt;
  using propertiest = ebmc_propertiest::propertiest;

  propertiest properties;

  bool all_properties_proved() const
  {
    for(const auto &p : properties)
    {
      if(p.is_assumption())
      {
        // ignore
      }
      else if(
        !p.is_proved() && !p.is_proved_with_bound() && !p.is_disabled() &&
        !p.is_assumed())
      {
        return false;
      }
    }

    return true;
  }

  bool has_unknown_property() const
  {
    for(const auto &p : properties)
      if(p.is_unknown())
        return true;

    return false;
  }

  // command-line tool exit code, depending on property status
  int exit_code() const;
};

property_checker_resultt property_checker(
  const cmdlinet &,
  transition_systemt &,
  ebmc_propertiest &,
  message_handlert &);

/// Certify results from an already solved batch. Include every active original
/// assumption (even when only one goal is selected), with normalized expressions
/// from this same model. BMC callers pass the original property depth, including
/// zero. Unbounded callers pass nullopt. This updates verdicts and traces in place.
void certify_property_witnesses(
  const cmdlinet &, const transition_systemt &, property_checker_resultt &,
  std::optional<std::size_t> bmc_bound, message_handlert &);

/// Encode one normalized property from the batch used to build the netlist.
/// Populates exactly bound+1 literals, asserts assumptions and freezes goals.
/// Returns false for an unsupported property and records its diagnostic.
/// Retain unsupported assumptions in the batch presented for certification.
bool compile_bounded_netlist_property(
  const cmdlinet &, ebmc_propertiest::propertyt &, const class netlistt &,
  const class bmc_mapt &, std::size_t bound, const namespacet &, class cnft &,
  message_handlert &);

/// Solve one compiled goal. Applies assert/cover polarity and records the
/// bounded verdict and optional finite trace. Certify the original result
/// batch before publishing witnesses.
void solve_bounded_netlist_property(
  const cmdlinet &, ebmc_propertiest::propertyt &, const class bmc_mapt &,
  std::size_t bound, const namespacet &, class propt &, message_handlert &,
  bool record_trace = true);

#endif
