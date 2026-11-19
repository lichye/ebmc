/*******************************************************************\

Module: Verilog Expression Simplifier

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_VERILOG_SIMPLIFIER_H
#define CPROVER_VERILOG_SIMPLIFIER_H

class exprt;
class namespacet;

/// Includes struct, union and array literals with constant members.
bool verilog_is_constant(const exprt &);

exprt verilog_simplifier(exprt, const namespacet &);

#endif
