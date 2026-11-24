/*******************************************************************\

Module: Output AIGER

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#ifndef CPROVER_EBMC_OUTPUT_AIGER_H
#define CPROVER_EBMC_OUTPUT_AIGER_H

#include <string>

class ebmc_propertiest;
class netlistt;

/// Validate the selected properties and write the netlist in AIGER format.
void output_aiger(
  const netlistt &,
  const ebmc_propertiest &,
  const std::string &filename);

#endif // CPROVER_EBMC_OUTPUT_AIGER_H
