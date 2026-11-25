/*******************************************************************\

Module: Netlists for EBMC

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#include "netlist.h"

#include <trans-netlist/netlist.h>
#include <trans-netlist/trans_to_netlist.h>
#include <trans-netlist/trans_to_netlist_simple.h>

#include "instrument_past.h"

netlistt make_netlist(
  transition_systemt &transition_system,
  ebmc_propertiest &properties,
  const cmdlinet &cmdline,
  message_handlert &message_handler)
{
  instrument_past(transition_system, properties);

  return make_netlist(
    transition_system.symbol_table,
    transition_system.main_symbol->name,
    transition_system.trans_expr,
    properties.make_property_map(),
    cmdline,
    message_handler);
}

netlistt make_netlist(
  symbol_table_baset &symbol_table,
  const irep_idt &module,
  const transt &trans,
  const std::map<irep_idt, exprt> &properties,
  const cmdlinet &cmdline,
  message_handlert &message_handler,
  const std::unordered_set<irep_idt> *retained_symbols)
{
  netlistt netlist;

  if(cmdline.isset("simple-netlist"))
  {
    convert_trans_to_netlist_simple(
      symbol_table,
      module,
      trans,
      properties,
      netlist,
      message_handler,
      retained_symbols);
  }
  else
  {
    convert_trans_to_netlist(
      symbol_table,
      module,
      trans,
      properties,
      netlist,
      message_handler,
      retained_symbols);
  }

  // check that the AIG is in dependency order
  netlist.check_ordering();

  return netlist;
}
