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
/// Nets with a statically sized bit representation can be resolved per bit.
bool is_tristate_net_type(const typet &);

void verilog_resolve_nets(
  const verilog_rtlt &,
  symbol_table_baset &,
  const irep_idt &module,
  transt &,
  const std::function<exprt(exprt)> &lower);

#endif
