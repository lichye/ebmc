/*******************************************************************\

Module: Netlists for EBMC

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#ifndef CPROVER_EBMC_NETLIST_H
#define CPROVER_EBMC_NETLIST_H

#include "ebmc_properties.h"
#include "transition_system.h"

#include <unordered_set>

class netlistt;

netlistt make_netlist(
  transition_systemt &,
  ebmc_propertiest &,
  const cmdlinet &,
  message_handlert &);

/// Convert an already instrumented relation, optionally with a smaller set
/// of variables to allocate in the netlist.
netlistt make_netlist(
  symbol_table_baset &,
  const irep_idt &module,
  const transt &,
  const std::map<irep_idt, exprt> &properties,
  const cmdlinet &,
  message_handlert &,
  const std::unordered_set<irep_idt> *retained_symbols = nullptr);

#endif // CPROVER_EBMC_NETLIST_H
