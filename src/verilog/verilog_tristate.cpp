/*******************************************************************\

Module: Verilog net resolution and charge storage (IEEE 1800, 6.6 and 28)

\*******************************************************************/

#include "verilog_tristate.h"

#include <util/arith_tools.h>
#include <util/mathematical_expr.h>
#include <util/symbol_table_base.h>

#include "aval_bval_encoding.h"
#include "verilog_bits.h"
#include "verilog_lowering.h"
#include "verilog_rtl.h"
#include "verilog_types.h"

#include <algorithm>

bool is_tristate_switch(const irep_idt &id)
{
  return id == ID_bufif0 || id == ID_bufif1 || id == ID_notif0 ||
         id == ID_notif1 || id == ID_nmos || id == ID_pmos || id == ID_rnmos ||
         id == ID_rpmos || id == ID_cmos || id == ID_rcmos;
}

bool is_tristate_net_type(const typet &type)
{
  if(type.id() == ID_struct || type.id() == ID_union || type.id() == ID_array)
    return verilog_bits_opt(type).has_value();
  return type.id() == ID_bool || type.id() == ID_unsignedbv ||
         type.id() == ID_signedbv;
}

namespace
{
std::size_t width_of(const exprt &value)
{
  if(value.type().id() == ID_bv)
    return to_bv_type(value.type()).get_width();
  return numeric_cast_v<std::size_t>(verilog_bits(value.type()));
}

exprt from_bits(exprt bits, const typet &type)
{
  if(type.id() == ID_bool)
    return extractbit_exprt{std::move(bits), 0};
  return verilog_lowering_cast(typecast_exprt{std::move(bits), type});
}

typet encoded_type(std::size_t width)
{
  return lower_to_aval_bval(verilog_unsignedbv_typet{width});
}

exprt pack(exprt a, exprt b)
{
  auto type = encoded_type(to_bv_type(a.type()).get_width());
  return concatenation_exprt{{std::move(b), std::move(a)}, std::move(type)};
}

exprt high_z(std::size_t width)
{
  auto ones = bv_typet{width}.all_ones_expr();
  return pack(ones, ones);
}

// Collect positive evidence for a driven 0, 1, or x independently for
// each bit. z supplies no evidence. This also handles an empty driver set.
exprt resolve(const std::vector<exprt> &drivers, std::size_t width)
{
  if(drivers.empty())
    return high_z(width);
  if(drivers.size() == 1)
    return drivers.front();
  const auto zero = bv_typet{width}.all_zeros_expr();
  exprt driven0 = zero, driven1 = zero, driven_x = zero;
  for(const auto &d : drivers)
  {
    auto a = aval(d), b = bval(d);
    driven0 = bitor_exprt{driven0, bitnot_exprt{bitor_exprt{a, b}}};
    driven1 = bitor_exprt{driven1, bitand_exprt{a, bitnot_exprt{b}}};
    driven_x = bitor_exprt{driven_x, bitand_exprt{bitnot_exprt{a}, b}};
  }
  auto z = bitnot_exprt{bitor_exprt{bitor_exprt{driven0, driven1}, driven_x}};
  auto x = bitor_exprt{driven_x, bitand_exprt{driven0, driven1}};
  return pack(
    bitor_exprt{z, bitand_exprt{driven1, bitnot_exprt{x}}}, bitor_exprt{z, x});
}

// Each bit of a gate array has its own control. A disabled switch releases
// the line. With unknown control only a MOS device can pass a definite z.
exprt switch_drive(const irep_idt &id, const exprt &data, const exprt &control)
{
  const bool low =
    id == ID_bufif0 || id == ID_notif0 || id == ID_pmos || id == ID_rpmos;
  const bool invert = id == ID_notif0 || id == ID_notif1;
  const bool mos =
    id == ID_nmos || id == ID_pmos || id == ID_rnmos || id == ID_rpmos;
  exprt enable = aval(control);
  if(low)
    enable = bitnot_exprt{enable};
  auto unknown = bval(control);
  auto on = bitand_exprt{enable, bitnot_exprt{unknown}};
  auto off = bitnot_exprt{bitor_exprt{enable, unknown}};
  auto a = aval(data), b = bval(data);
  if(invert)
    a = bitnot_exprt{bitor_exprt{a, b}};
  exprt out_a = bitor_exprt{off, bitand_exprt{on, a}};
  if(mos)
    out_a = bitor_exprt{out_a, bitand_exprt{unknown, bitand_exprt{a, b}}};
  return pack(
    out_a, bitor_exprt{bitor_exprt{off, unknown}, bitand_exprt{on, b}});
}

using loweringt = std::function<exprt(exprt)>;
using encoding_mapt = std::map<irep_idt, exprt>;

class driver_valuet
{
public:
  driver_valuet(const encoding_mapt &nets, const loweringt &lower)
    : nets(nets), lower(lower)
  {
  }
  exprt operator()(const exprt &e, std::size_t width) const
  {
    return aval_bval_conversion(value(e), encoded_type(width));
  }

private:
  const encoding_mapt &nets;
  const loweringt &lower;

  exprt value(const exprt &e) const
  {
    const auto width = width_of(e);
    const auto &id = e.id();
    if(id == ID_symbol)
    {
      auto found = nets.find(to_symbol_expr(e).get_identifier());
      if(found != nets.end())
        return found->second;
    }
    else if(is_tristate_switch(id))
    {
      auto data = (*this)(e.operands()[0], width);
      auto control = (*this)(e.operands()[1], width);
      if(id == ID_cmos || id == ID_rcmos)
        return resolve(
          {switch_drive(ID_nmos, data, control),
           switch_drive(ID_pmos, data, (*this)(e.operands()[2], width))},
          width);
      return switch_drive(id, data, control);
    }
    else if(id == ID_typecast)
    {
      const auto &op = to_typecast_expr(e).op();
      if(verilog_bits_opt(op.type()))
      {
        auto raw = value(op);
        if(
          op.type().id() == ID_signedbv ||
          op.type().id() == ID_verilog_signedbv)
          raw.type() = lower_to_aval_bval(
            verilog_signedbv_typet{aval_bval_width(raw.type())});
        return aval_bval_conversion(raw, encoded_type(width));
      }
    }
    else if(id == ID_if)
    {
      const auto &mux = to_if_expr(e);
      const auto cond = value(mux.cond());
      auto a = (*this)(mux.true_case(), width),
           b = (*this)(mux.false_case(), width);
      auto zeros = bv_typet{aval_bval_width(cond.type())}.all_zeros_expr();
      auto true_cond = notequal_exprt{
        bitand_exprt{aval(cond), bitnot_exprt{bval(cond)}}, zeros};
      auto false_cond = equal_exprt{bitor_exprt{aval(cond), bval(cond)}, zeros};
      auto differ = bitor_exprt{
        bitxor_exprt{aval(a), aval(b)}, bitxor_exprt{bval(a), bval(b)}};
      auto merged = pack(
        bitand_exprt{aval(a), bitnot_exprt{differ}},
        bitor_exprt{bval(a), differ});
      return if_exprt{true_cond, a, if_exprt{false_cond, b, merged}};
    }
    else if(id == ID_concatenation)
    {
      exprt::operandst parts;
      for(const auto &op : e.operands())
        parts.push_back(value(op));
      return aval_bval_concatenation(parts, encoded_type(width));
    }
    else if(id == ID_replication)
    {
      auto &rep = to_replication_expr(e);
      auto raw = value(rep.op());
      bv_typet type{width};
      return pack(
        replication_exprt{rep.times(), aval(raw), type},
        replication_exprt{rep.times(), bval(raw), type});
    }
    else if(
      id == ID_extractbit || id == ID_extractbits || id == ID_member ||
      id == ID_index || id == ID_verilog_bit_select ||
      id == ID_verilog_non_indexed_part_select ||
      id == ID_verilog_indexed_part_select_plus ||
      id == ID_verilog_indexed_part_select_minus)
    {
      auto &src = e.operands().front();
      if(
        is_tristate_net_type(src.type()) || is_four_valued(src) ||
        src.type().id() == ID_bv)
      {
        auto raw = value(src);
        exprt selection = e;
        for(std::size_t i = 1; i < selection.operands().size(); ++i)
          selection.operands()[i] = lower(selection.operands()[i]);
        selection.type() = make_two_valued(e.type());
        auto select = [&](exprt bits)
        {
          exprt result = selection;
          result.operands().front() =
            id == ID_extractbit || id == ID_extractbits
              ? std::move(bits)
              : from_bits(std::move(bits), make_two_valued(src.type()));
          return typecast_exprt{
            to_bitvector(verilog_lowering_node(std::move(result))),
            bv_typet{width}};
        };
        return pack(select(aval(raw)), select(bval(raw)));
      }
    }
    return aval_bval_conversion(to_bitvector(lower(e)), encoded_type(width));
  }
};

// Pad bits outside this driver's lvalue with z, including the gaps
// between independently driven slices of a packed vector.
exprt place(exprt drive, const verilog_rtl_slicet &slice, std::size_t width)
{
  const auto hi = numeric_cast_v<std::size_t>(slice.higher);
  const auto lo = numeric_cast_v<std::size_t>(slice.lower);
  if(hi + 1 == width && lo == 0)
    return drive;
  exprt::operandst parts;
  if(hi + 1 < width)
    parts.push_back(high_z(width - 1 - hi));
  parts.push_back(std::move(drive));
  if(lo)
    parts.push_back(high_z(lo));
  return aval_bval_concatenation(parts, encoded_type(width));
}

// For a direct connection, preserve the source net's sampled value as
// well as its four-state drive. This also keeps aliases of a floating
// wire consistent within a frame.
bool is_connection(const exprt &e)
{
  if(e.id() == ID_symbol)
    return true;
  if(e.id() == ID_concatenation)
    return std::all_of(e.operands().begin(), e.operands().end(), is_connection);
  if(
    e.id() == ID_typecast || e.id() == ID_member || e.id() == ID_index ||
    e.id() == ID_extractbit || e.id() == ID_extractbits ||
    e.id() == ID_verilog_bit_select ||
    e.id() == ID_verilog_non_indexed_part_select ||
    e.id() == ID_verilog_indexed_part_select_plus ||
    e.id() == ID_verilog_indexed_part_select_minus)
    return is_connection(e.operands().front());
  return false;
}

// Read the default (pull or stored charge) only on bits all drivers release.
exprt when_released(const exprt &drive, const exprt &fallback)
{
  if(fallback.is_nil())
    return drive;
  auto z = bitand_exprt{aval(drive), bval(drive)};
  auto select = [&](exprt driven, exprt released)
  {
    return bitor_exprt{
      bitand_exprt{bitnot_exprt{z}, std::move(driven)},
      bitand_exprt{z, std::move(released)}};
  };
  return pack(
    select(aval(drive), aval(fallback)), select(bval(drive), bval(fallback)));
}

exprt bit_at(const exprt &bits, std::size_t i)
{
  return extractbits_exprt{bits, i, bv_typet{1}};
}

exprt drive_bit(const exprt &drive, std::size_t i)
{
  return pack(bit_at(aval(drive), i), bit_at(bval(drive), i));
}

struct net_valuet
{
  exprt drive;
  exprt fallback;
  exprt choice;
};

// A collapsed inout bit has one set of drivers, one default, and one
// two-state sample, regardless of how many names it has in the hierarchy.
using connected_bitst = std::map<std::size_t, std::pair<exprt, exprt>>;
using connection_mapt = std::map<irep_idt, connected_bitst>;

connection_mapt resolve_inouts(
  const verilog_rtlt &rtl,
  const std::map<irep_idt, net_valuet> &local)
{
  connection_mapt result;
  for(const auto &group : rtl.inout_groups)
  {
    std::vector<exprt> drives, defaults;
    for(const auto &bit : group.second)
    {
      const auto &net = local.at(bit.first);
      drives.push_back(drive_bit(net.drive, bit.second));
      if(net.fallback.is_not_nil())
        defaults.push_back(drive_bit(net.fallback, bit.second));
    }
    auto drive = resolve(drives, 1);
    if(!defaults.empty())
      drive = when_released(drive, resolve(defaults, 1));
    const auto &representative = group.second.front();
    auto choice =
      bit_at(local.at(representative.first).choice, representative.second);
    for(const auto &bit : group.second)
      result[bit.first].emplace(bit.second, std::make_pair(drive, choice));
  }
  return result;
}
} // namespace

void verilog_resolve_nets(
  const verilog_rtlt &rtl,
  symbol_table_baset &symbols,
  const irep_idt &module,
  transt &trans,
  const loweringt &lower)
{
  encoding_mapt nets;
  auto auxiliary = [&](
                     const symbolt &net,
                     const std::string &suffix,
                     typet type,
                     bool state = false)
  {
    const auto name = id2string(net.name) + "::" + suffix;
    symbolt symbol{name, std::move(type), net.mode};
    symbol.base_name = id2string(net.base_name) + "::" + suffix;
    symbol.pretty_name = id2string(net.display_name()) + "::" + suffix;
    symbol.location = net.location;
    symbol.module = module;
    symbol.is_auxiliary = true;
    symbol.is_state_var = state;
    auto expr = symbol.symbol_expr();
    CHECK_RETURN(!symbols.add(symbol));
    return expr;
  };

  std::set<irep_idt> connected;
  for(const auto &group : rtl.inout_groups)
    for(const auto &bit : group.second)
      connected.insert(bit.first);

  // Allocate the encoded signals first: module and declaration order do
  // not determine which signals another driver can reference.
  for(const auto &entry : rtl.resolved_nets)
  {
    const auto &net = symbols.lookup_ref(entry.first);
    const auto width = width_of(net.symbol_expr());
    const auto kind = net.type.get("#net_kind");
    if(
      entry.second.empty() && !connected.count(entry.first) &&
      kind != ID_trireg && kind != ID_tri0 && kind != ID_tri1)
      nets.emplace(entry.first, high_z(width));
    else
      nets.emplace(entry.first, auxiliary(net, "drive", encoded_type(width)));
  }

  driver_valuet driver(nets, lower);
  std::map<irep_idt, net_valuet> local;
  for(const auto &entry : rtl.resolved_nets)
  {
    if(nets.at(entry.first).id() != ID_symbol)
      continue; // An undriven ordinary net already reads as a free input.
    const auto &net = symbols.lookup_ref(entry.first);
    const auto visible = net.symbol_expr();
    const auto width = width_of(visible);
    const bv_typet bits{width};
    std::vector<exprt> drives;
    for(const auto &d : entry.second)
      drives.push_back(place(
        driver(d.value, numeric_cast_v<std::size_t>(d.slice.width())),
        d.slice,
        width));
    auto drive = resolve(drives, width);
    if(connected.count(entry.first) && !drives.empty())
    {
      // Keep a single word-level definition shared by all connected bits.
      auto wire = auxiliary(net, "local_drive", encoded_type(width));
      trans.invar().add_to_operands(equal_exprt{wire, drive});
      drive = std::move(wire);
    }

    exprt fallback = nil_exprt{};
    const auto kind = net.type.get("#net_kind");
    if(kind == ID_trireg)
    {
      auto saved = auxiliary(net, "charge", visible.type(), true);
      fallback =
        pack(typecast_exprt{to_bitvector(saved), bits}, bits.all_zeros_expr());
      // Sample the public value once, including when the drivers conflict.
      saved.id(ID_next_symbol);
      trans.trans().add_to_operands(equal_exprt{saved, visible});
    }
    else if(kind == ID_tri0 || kind == ID_tri1)
      fallback = pack(
        kind == ID_tri1 ? bits.all_ones_expr() : bits.all_zeros_expr(),
        bits.all_zeros_expr());

    // The free choice is one combinational signal per net. All consumers
    // see the same sample in a frame, and a new frame can choose again.
    const bool alias = !connected.count(entry.first) &&
                       entry.second.size() == 1 &&
                       entry.second.front().slice.width() == width &&
                       is_connection(entry.second.front().value);
    exprt choice = alias ? lower(entry.second.front().value)
                         : auxiliary(net, "read", visible.type());
    local.emplace(
      entry.first,
      net_valuet{
        std::move(drive),
        std::move(fallback),
        typecast_exprt{to_bitvector(choice), bits}});
  }

  const auto connections = resolve_inouts(rtl, local);
  for(auto &entry : local)
  {
    const auto &raw = nets.at(entry.first);
    const auto visible = symbols.lookup_ref(entry.first).symbol_expr();
    const auto width = width_of(visible);
    auto &net = entry.second;
    auto drive = when_released(net.drive, net.fallback);
    auto choice = net.choice;
    const auto connection = connections.find(entry.first);
    if(connection != connections.end())
    {
      exprt::operandst drives, choices;
      for(std::size_t i = width; i-- != 0;)
      {
        auto bit = connection->second.find(i);
        drives.push_back(
          bit == connection->second.end() ? drive_bit(drive, i)
                                          : bit->second.first);
        choices.push_back(
          bit == connection->second.end() ? bit_at(choice, i)
                                          : bit->second.second);
      }
      drive = aval_bval_concatenation(drives, encoded_type(width));
      choice = choices.size() == 1
                 ? choices.front()
                 : concatenation_exprt{std::move(choices), bv_typet{width}};
    }
    trans.invar().add_to_operands(equal_exprt{raw, drive});
    auto read = bitor_exprt{
      bitand_exprt{aval(raw), bitnot_exprt{bval(raw)}},
      bitand_exprt{std::move(choice), bval(raw)}};
    auto projected = from_bits(std::move(read), visible.type());
    trans.invar().add_to_operands(equal_exprt{visible, std::move(projected)});
  }
}
