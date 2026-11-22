#ifndef CPROVER_EBMC_INSTRUMENT_REGULAR_SEQUENCE_H
#define CPROVER_EBMC_INSTRUMENT_REGULAR_SEQUENCE_H

#include "ebmc_properties.h"
#include "transition_system.h"

/// Install a functional safety monitor for a nonempty regular assertion.
bool instrument_regular_sequence(
  transition_systemt &,
  ebmc_propertiest::propertyt &,
  const symbolt &property_symbol);

#endif
