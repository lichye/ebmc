/*******************************************************************\

Module: Verilog Register-Transfer Level Representation

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_VERILOG_VERILOG_RTL_H
#define CPROVER_VERILOG_VERILOG_RTL_H

#include <util/expr.h>
#include <util/invariant.h>
#include <util/mp_arith.h>
#include <util/symbol.h>

#include "verilog_standard.h"
#include "verilog_undriven_report.h"

#include <iosfwd>
#include <map>
#include <set>
#include <vector>

class message_handlert;
class namespacet;
class symbol_table_baset;

/// A contiguous range of bits of an identifier, given as
/// zero-based bit indices [lower, higher], both inclusive.
///
/// The indices refer to the bit-level representation of the identifier's
/// type, independently of the declared index ranges: bit 0 is the least
/// significant bit, as used by extractbits_exprt and concatenation_exprt.
/// For unpacked arrays, the elements are stored starting from the left
/// index of the declared range, i.e., the element with the left index
/// occupies the least significant bits. For packed arrays, the element
/// with the left index of the declared range is the most significant
/// (1800-2017 7.4.1). The first member of a packed struct is the most
/// significant; all members of a packed union start at bit 0.
class verilog_rtl_slicet
{
public:
  verilog_rtl_slicet(mp_integer _lower, mp_integer _higher)
    : lower(std::move(_lower)), higher(std::move(_higher))
  {
    PRECONDITION(lower <= higher);
  }

  mp_integer lower, higher;

  mp_integer width() const
  {
    return higher - lower + 1;
  }

  /// do the two slices share at least one bit?
  bool overlaps(const verilog_rtl_slicet &other) const
  {
    return lower <= other.higher && other.lower <= higher;
  }

  bool operator<(const verilog_rtl_slicet &other) const
  {
    if(lower != other.lower)
      return lower < other.lower;
    else
      return higher < other.higher;
  }

  bool operator==(const verilog_rtl_slicet &other) const
  {
    return lower == other.lower && higher == other.higher;
  }
};

/// The definition of a slice of an identifier: the slice is either
/// state-holding (a register) or a wire, together with the defining
/// expression.
class verilog_rtl_definitiont
{
public:
  enum class kindt
  {
    STATE_HOLDING,
    WIRE
  };

  kindt kind;

  /// For state-holding slices, the value of the slice in the *next*
  /// state; for wires, the value of the slice in the *current* state.
  exprt value;

  /// For wires, the slices of the same identifier whose reads in
  /// \ref value close a combinational cycle back to this slice, i.e.,
  /// the slices that are in a dependency cycle with this one. Includes
  /// the slice itself when \ref value reads it directly. Empty iff the
  /// slice does not depend on itself. Reads of other slices of the
  /// same identifier are well-founded, and are not listed.
  std::vector<verilog_rtl_slicet> cyclic_slices;

  source_locationt driver_location;

  verilog_rtl_definitiont(kindt _kind, exprt _value, source_locationt _location)
    : kind(_kind),
      value(std::move(_value)),
      driver_location(std::move(_location))
  {
  }

  bool is_state_holding() const
  {
    return kind == kindt::STATE_HOLDING;
  }

  bool is_wire() const
  {
    return kind == kindt::WIRE;
  }
};

/// An assert, assume or cover property, with an optional label.
class verilog_rtl_propertyt
{
public:
  enum class kindt
  {
    ASSERT,
    ASSUME,
    COVER
  };

  /// the context in which the property occurs
  enum class contextt
  {
    MODULE_LEVEL,
    ALWAYS,
    INITIAL
  };

  kindt kind;
  contextt context;

  /// the full identifier created by the type checker
  irep_idt identifier;

  /// the label of the property, or empty if none was given
  irep_idt label;

  /// the condition, with values substituted and function
  /// calls expanded
  exprt condition;

  /// the condition as written in the source, for display
  exprt source_condition;

  /// distinguishes 'cover sequence' from 'cover property'
  bool is_sequence = false;

  verilog_rtl_propertyt(
    kindt _kind,
    contextt _context,
    irep_idt _identifier,
    irep_idt _label,
    exprt _condition,
    exprt _source_condition)
    : kind(_kind),
      context(_context),
      identifier(std::move(_identifier)),
      label(std::move(_label)),
      condition(std::move(_condition)),
      source_condition(std::move(_source_condition))
  {
  }

  bool is_assert() const
  {
    return kind == kindt::ASSERT;
  }

  bool is_assume() const
  {
    return kind == kindt::ASSUME;
  }

  bool is_cover() const
  {
    return kind == kindt::COVER;
  }
};

enum class verilog_undriven_modet
{
  SEQ,
  CONST
};

/// The register-transfer level (RTL) representation of a Verilog
/// module: a map from identifiers to a map from slices to the
/// definition of the slice, the initial values, further constraints,
/// and the assert, assume and cover properties of the module.
/// Module instances are included recursively. This representation
/// follows type checking and elaboration, and precedes synthesis.
class verilog_rtlt
{
public:
  using slice_mapt = std::map<verilog_rtl_slicet, verilog_rtl_definitiont>;
  using identifier_mapt = std::map<irep_idt, slice_mapt>;
  using slice_value_mapt = std::map<verilog_rtl_slicet, exprt>;

  identifier_mapt identifier_map;

  /// Statically undriven ranges, in least-significant-bit coordinates.
  using undriven_mapt = std::map<irep_idt, std::vector<verilog_rtl_slicet>>;
  undriven_mapt undriven;

  struct drivert
  {
    verilog_rtl_slicet slice;
    exprt value;
  };

  // Nets whose drivers must retain x/z until resolution. An empty list
  // denotes an undriven net. Ordinary logic reads the public two-state net.
  std::map<irep_idt, std::vector<drivert>> resolved_nets;
  /// Resolution points for Z-capable co-drivers, pull defaults or stored charge.
  std::set<irep_idt> merging_nets;
  /// Overlapping drivers whose disagreement makes a public bit unconstrained.
  std::set<irep_idt> binary_conflicts;
  /// Conditional drivers whose unknown control may leave some output bits known.
  std::set<irep_idt> ambiguous_drives;
  /// Ordinary expressions can propagate explicit X/Z, but a floating bit
  /// supplies its public two-state input outside a physical driver.
  std::set<irep_idt> literal_nets;
  /// Physical sources that can present high impedance to another driver.
  std::set<irep_idt> may_release;

  /// Each group denotes a single physical bit connected by inout ports.
  using net_bitt = std::pair<irep_idt, std::size_t>;
  std::map<std::size_t, std::vector<net_bitt>> inout_groups;

  /// the declared state-holding variables, including those
  /// that are not assigned; unassigned variables hold their value
  std::set<irep_idt> variables;

  /// identifiers that are forced to a value, e.g. by a port
  /// connection; these become wires
  std::set<irep_idt> forced;

  /// the initial values, from initial constructs and variable
  /// declarations with an initializer
  std::map<irep_idt, slice_value_mapt> initial_values;

  /// Boolean constraints that hold in every state,
  /// e.g. from primitive gates
  std::vector<exprt> constraints;

  /// the properties, in the order in which they appear in the module
  std::vector<verilog_rtl_propertyt> properties;

  /// Auxiliary wires that hold large values of procedural assignments,
  /// which would otherwise be written out in each place they are used.
  /// Their definitions are in \ref identifier_map. Their symbols are
  /// not in the symbol table that the representation was built from,
  /// and are given in \ref auxiliary_symbols, in the same order.
  std::set<irep_idt> auxiliary_wires;
  std::vector<symbolt> auxiliary_symbols;

  void output(const namespacet &, std::ostream &) const;
  std::string as_string(const namespacet &) const;
};

/// Construct the RTL representation of the given type-checked
/// module. Throws ebmc_errort on failure.
verilog_rtlt verilog_rtl(
  const symbol_table_baset &,
  const irep_idt &module_identifier,
  verilog_standardt,
  message_handlert &,
  verilog_undriven_modet = verilog_undriven_modet::SEQ,
  const verilog_undriven_reportert & = {});

#endif // CPROVER_VERILOG_VERILOG_RTL_H
