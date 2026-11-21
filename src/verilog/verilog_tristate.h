/*******************************************************************\

Module: Verilog net resolution and charge storage

\*******************************************************************/

#ifndef CPROVER_VERILOG_TRISTATE_H
#define CPROVER_VERILOG_TRISTATE_H

#include <util/expr.h>

#include <functional>

class symbol_table_baset;
class transt;
class verilog_rtlt;

bool is_tristate_switch(const irep_idt &);
bool is_verilog_drive_operator(const irep_idt &);
bool is_verilog_unknown_literal(const exprt &, bool release_only = false);
bool is_verilog_net_connection(const exprt &);
/// Nets with a statically sized bit representation can be resolved per bit.
bool is_tristate_net_type(const typet &);

/// Sample a single driver at its net. Each unknown bit is a fresh two-state
/// input, shared by every read of that net in this time frame.
exprt verilog_sample_drive(
  const exprt &,
  const std::function<exprt(const typet &)> &fresh);

void verilog_resolve_nets(
  verilog_rtlt &,
  symbol_table_baset &,
  const irep_idt &module,
  transt &,
  const std::function<exprt(exprt)> &lower);

#endif
