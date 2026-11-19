/*******************************************************************\

Module: Verilog net resolution and charge storage (IEEE 1800, 6.6 and 28)

\*******************************************************************/

#include "verilog_tristate.h"

#include <util/arith_tools.h>
#include <util/expr_util.h>
#include <util/mathematical_expr.h>
#include <util/namespace.h>
#include <util/simplify_expr.h>
#include <util/symbol_table_base.h>

#include "aval_bval_encoding.h"
#include "verilog_bits.h"
#include "verilog_lowering.h"
#include "verilog_rtl.h"
#include "verilog_types.h"

#include <algorithm>
#include <unordered_map>

bool is_tristate_switch(const irep_idt &id)
{
  return id == ID_bufif0 || id == ID_bufif1 || id == ID_notif0 ||
         id == ID_notif1 || id == ID_nmos || id == ID_pmos || id == ID_rnmos ||
         id == ID_rpmos || id == ID_cmos || id == ID_rcmos;
}

bool is_verilog_unknown_literal(const exprt &e, bool release_only)
{
  return (e.is_constant() || e.id() == ID_verilog_unbased_unsized_literal ||
          e.id() == ID_verilog_based_unsized_literal) &&
         is_four_valued(e) &&
         e.get_string(ID_value).find_first_of(release_only ? "z?" : "xz?") !=
           std::string::npos;
}

bool is_verilog_drive_operator(const irep_idt &id)
{
  return id == ID_bitand || id == ID_bitor || id == ID_bitxor ||
         id == ID_bitxnor || id == ID_bitnot || id == ID_and || id == ID_or ||
         id == ID_not || id == ID_xor || id == ID_xnor || id == ID_nand ||
         id == ID_nor || id == ID_reduction_and || id == ID_reduction_or ||
         id == ID_reduction_nand || id == ID_reduction_nor ||
         id == ID_reduction_xor || id == ID_reduction_xnor ||
         id == ID_unary_minus || id == ID_unary_plus || id == ID_plus ||
         id == ID_minus || id == ID_mult || id == ID_div || id == ID_mod ||
         id == ID_power || id == ID_shl || id == ID_lshr || id == ID_ashr ||
         id == ID_zero_extend || id == ID_equal || id == ID_notequal ||
         id == ID_verilog_logical_equality ||
         id == ID_verilog_logical_inequality ||
         id == ID_verilog_case_equality || id == ID_verilog_case_inequality ||
         id == ID_lt || id == ID_le || id == ID_gt || id == ID_ge;
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
using drive_lookupt = std::function<exprt(const irep_idt &)>;

class driver_valuet
{
public:
  driver_valuet(const drive_lookupt &lookup, const loweringt &lower)
    : lookup(lookup), lower(lower)
  {
  }
  exprt operator()(const exprt &e, std::size_t width) const
  {
    return aval_bval_conversion(value(e), encoded_type(width));
  }

private:
  const drive_lookupt &lookup;
  const loweringt &lower;

  exprt logical_operand(const exprt &e) const
  {
    // The type checker represents logical truth as a cast to bool.
    // A known one anywhere in the vector takes precedence over X/Z.
    if(
      e.id() == ID_typecast && e.type().id() == ID_bool &&
      verilog_bits_opt(to_typecast_expr(e).op().type()))
      return value(to_typecast_expr(e).op());
    return value(e);
  }

  exprt conditional(const if_exprt &mux, std::size_t width) const
  {
    const auto cond = logical_operand(mux.cond());
    auto a = (*this)(mux.true_case(), width),
         b = (*this)(mux.false_case(), width);
    auto zeros = bv_typet{aval_bval_width(cond.type())}.all_zeros_expr();
    exprt true_cond, ambiguous;
    if(aval_bval_width(cond.type()) == 1)
    {
      true_cond = extractbit_exprt{aval(cond), 0};
      ambiguous = extractbit_exprt{bval(cond), 0};
    }
    else
    {
      true_cond = notequal_exprt{
        bitand_exprt{aval(cond), bitnot_exprt{bval(cond)}}, zeros};
      ambiguous =
        and_exprt{not_exprt{true_cond}, notequal_exprt{bval(cond), zeros}};
    }
    auto mask_a = bval(a), mask_b = bval(b);
    auto data_a = aval(a), data_b = aval(b);
    auto choose = [&](const exprt &lhs, const exprt &rhs)
    {
      return lhs == rhs ? lhs
                        : static_cast<exprt>(if_exprt{true_cond, lhs, rhs});
    };
    // For an unknown selector only positions that agree survive (IEEE
    // 1800-2017, 11.4.11). Keep the masks separate from the data path.
    auto either_unknown = bitor_exprt{mask_a, mask_b};
    auto comparison_bits = [&](const exprt &arm, const exprt &data)
    {
      // Unknown bits are already in either_unknown. Compare public data
      // on their known neighbours without reintroducing the source masks.
      return is_verilog_net_connection(arm) && !is_four_valued(arm)
               ? typecast_exprt::conditional_cast(
                   to_bitvector(lower(arm)), bv_typet{width})
               : data;
    };
    auto differs = bitxor_exprt{
      comparison_bits(mux.true_case(), data_a),
      comparison_bits(mux.false_case(), data_b)};
    auto mask = if_exprt{
      ambiguous, bitor_exprt{either_unknown, differs}, choose(mask_a, mask_b)};
    auto agreed_one_or_z = bitand_exprt{
      bitand_exprt{data_a, data_b}, bitnot_exprt{bitxor_exprt{mask_a, mask_b}}};
    auto data = if_exprt{ambiguous, agreed_one_or_z, choose(data_a, data_b)};
    return pack(std::move(data), std::move(mask));
  }

  exprt logic(const exprt &e, std::size_t width) const
  {
    const auto &id = e.id();
    exprt op = e;
    for(auto &arg : op.operands())
    {
      auto t = arg.type();
      auto v = id == ID_and || id == ID_or || id == ID_not
                 ? logical_operand(arg)
                 : value(arg);
      if(t.id() == ID_signedbv || t.id() == ID_verilog_signedbv)
        v.type() =
          lower_to_aval_bval(verilog_signedbv_typet{aval_bval_width(v.type())});
      arg = std::move(v);
    }
    if(id == ID_not)
    {
      const auto &arg = op.operands().front();
      const auto zeros = to_bv_type(aval(arg).type()).all_zeros_expr();
      auto no_one =
        equal_exprt{bitand_exprt{aval(arg), bitnot_exprt{bval(arg)}}, zeros};
      const bv_typet bit{1};
      return pack(
        typecast_exprt{and_exprt{no_one, equal_exprt{bval(arg), zeros}}, bit},
        typecast_exprt{
          and_exprt{no_one, notequal_exprt{bval(arg), zeros}}, bit});
    }
    if(id == ID_xor || id == ID_xnor)
      op.id(id == ID_xor ? ID_bitxor : ID_bitxnor);
    if(id == ID_nand || id == ID_nor)
    {
      op.id(id == ID_nand ? ID_bitand : ID_bitor);
      op.type() = verilog_unsignedbv_typet{1};
      auto raw = verilog_lowering_node(std::move(op));
      return pack(bitnot_exprt{bitor_exprt{aval(raw), bval(raw)}}, bval(raw));
    }
    if(
      id == ID_equal || id == ID_notequal ||
      id == ID_verilog_logical_equality || id == ID_verilog_logical_inequality)
    {
      const auto &lhs = op.operands()[0], &rhs = op.operands()[1];
      auto mask = bitor_exprt{bval(lhs), bval(rhs)};
      auto zeros = to_bv_type(mask.type()).all_zeros_expr();
      auto differ = notequal_exprt{
        bitand_exprt{bitxor_exprt{aval(lhs), aval(rhs)}, bitnot_exprt{mask}},
        zeros};
      auto eq = id == ID_equal || id == ID_verilog_logical_equality;
      const bv_typet bit{1};
      return pack(
        typecast_exprt{
          eq ? static_cast<exprt>(
                 and_exprt{not_exprt{differ}, equal_exprt{mask, zeros}})
             : static_cast<exprt>(differ),
          bit},
        typecast_exprt{
          and_exprt{not_exprt{differ}, notequal_exprt{mask, zeros}}, bit});
    }
    if(id == ID_verilog_case_equality || id == ID_verilog_case_inequality)
    {
      auto eq = equal_exprt{op.operands()[0], op.operands()[1]};
      return aval_bval_conversion(
        id == ID_verilog_case_equality ? static_cast<exprt>(eq)
                                       : static_cast<exprt>(not_exprt{eq}),
        encoded_type(1));
    }
    if(id == ID_and || id == ID_or)
    {
      while(op.operands().size() > 2)
      {
        auto rhs = std::move(op.operands().back());
        op.operands().pop_back();
        auto &lhs = op.operands().back();
        exprt pair{id, verilog_unsignedbv_typet{1}, {lhs, rhs}};
        lhs = verilog_lowering_node(std::move(pair));
      }
    }
    op.type() =
      e.type().id() == ID_signedbv || e.type().id() == ID_verilog_signedbv
        ? static_cast<typet>(verilog_signedbv_typet{width})
        : static_cast<typet>(verilog_unsignedbv_typet{width});
    return verilog_lowering_node(std::move(op));
  }

  exprt value(const exprt &e) const
  {
    const auto width = width_of(e);
    const auto &id = e.id();
    if(id == ID_symbol)
    {
      auto found = lookup(to_symbol_expr(e).get_identifier());
      if(found.is_not_nil())
        return found;
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
      return conditional(to_if_expr(e), width);
    else if(is_verilog_drive_operator(id))
      return logic(e, width);
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
        exprt index_unknown = false_exprt{};
        if(
          e.operands().size() > 1 && !e.operands()[1].is_constant() &&
          verilog_bits_opt(e.operands()[1].type()))
          index_unknown = has_xz(value(e.operands()[1]));
        else if(
          e.operands().size() > 1 &&
          is_verilog_unknown_literal(e.operands()[1]))
          index_unknown = true_exprt{};
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
        auto selected = pack(select(aval(raw)), select(bval(raw)));
        if(index_unknown.is_false())
          return selected;
        const bv_typet word{width};
        return if_exprt{
          std::move(index_unknown),
          pack(word.all_zeros_expr(), word.all_ones_expr()),
          selected};
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

// Align a two-state driver slice. Outside its lvalue it contributes no
// evidence for zero, one, or unknown.
exprt place_bits(
  const exprt &bits,
  const verilog_rtl_slicet &slice,
  std::size_t width)
{
  const auto lo = numeric_cast_v<std::size_t>(slice.lower);
  const auto hi = numeric_cast_v<std::size_t>(slice.higher);
  if(lo == 0 && hi + 1 == width)
    return bits;
  exprt::operandst parts;
  if(hi + 1 < width)
    parts.push_back(bv_typet{width - hi - 1}.all_zeros_expr());
  parts.push_back(bits);
  if(lo)
    parts.push_back(bv_typet{lo}.all_zeros_expr());
  return concatenation_exprt{std::move(parts), bv_typet{width}};
}

} // namespace

// For a direct connection, preserve the source net's sampled value as
// well as its four-state drive. This also keeps aliases of a floating
// wire consistent within a frame.
bool is_verilog_net_connection(const exprt &e)
{
  if(e.id() == ID_symbol)
    return true;
  if(e.id() == ID_concatenation)
    return std::all_of(
      e.operands().begin(), e.operands().end(), is_verilog_net_connection);
  if(
    e.id() == ID_typecast || e.id() == ID_member || e.id() == ID_index ||
    e.id() == ID_extractbit || e.id() == ID_extractbits ||
    e.id() == ID_verilog_bit_select ||
    e.id() == ID_verilog_non_indexed_part_select ||
    e.id() == ID_verilog_indexed_part_select_plus ||
    e.id() == ID_verilog_indexed_part_select_minus)
    return is_verilog_net_connection(e.operands().front());
  return false;
}

namespace
{
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

exprt verilog_sample_drive(
  const exprt &e,
  const std::function<exprt(const typet &)> &fresh)
{
  const auto width = width_of(e);
  const auto type = make_two_valued(e.type());
  if(e.id() == ID_verilog_unbased_unsized_literal)
    return verilog_sample_drive(
      to_verilog_unbased_unsized_literal_expr(e).expand(), fresh);
  if(e.id() == ID_verilog_based_unsized_literal)
    return verilog_sample_drive(
      to_verilog_based_unsized_literal_expr(e).expand(), fresh);
  if(e.is_constant() && is_four_valued(e))
  {
    const auto &digits = id2string(to_constant_expr(e).get_value());
    exprt::operandst parts;
    for(std::size_t first = 0; first < digits.size();)
    {
      const bool known = digits[first] == '0' || digits[first] == '1';
      std::size_t end = first + 1;
      while(end < digits.size() &&
            (digits[end] == '0' || digits[end] == '1') == known)
        ++end;
      const unsignedbv_typet part_type{end - first};
      if(known)
        parts.push_back(constant_exprt{
          make_bvrep(
            end - first,
            [&](std::size_t i) { return digits[end - i - 1] == '1'; }),
          part_type});
      else
        parts.push_back(fresh(part_type));
      first = end;
    }
    exprt bits =
      parts.size() == 1
        ? std::move(parts.front())
        : concatenation_exprt{std::move(parts), unsignedbv_typet{width}};
    return typecast_exprt::conditional_cast(std::move(bits), type);
  }
  if(e.id() == ID_if)
  {
    const auto &mux = to_if_expr(e);
    if(mux.true_case() == mux.false_case())
      return verilog_sample_drive(mux.true_case(), fresh);
    return if_exprt{
      typecast_exprt::conditional_cast(mux.cond(), bool_typet{}),
      typecast_exprt::conditional_cast(
        verilog_sample_drive(mux.true_case(), fresh), type),
      typecast_exprt::conditional_cast(
        verilog_sample_drive(mux.false_case(), fresh), type)};
  }
  if(e.id() == ID_replication)
  {
    const auto &rep = to_replication_expr(e);
    if(is_four_valued(rep.op()) && rep.op().is_constant())
    {
      // Each X/Z output bit is independent, including replicated literals.
      const auto count = numeric_cast_v<std::size_t>(rep.times());
      const auto &pattern = id2string(to_constant_expr(rep.op()).get_value());
      std::string digits;
      digits.reserve(width);
      for(std::size_t i = 0; i < count; ++i)
        digits += pattern;
      return verilog_sample_drive(
        constant_exprt{digits, verilog_unsignedbv_typet{width}}, fresh);
    }
    auto part = verilog_sample_drive(rep.op(), fresh);
    if(has_subexpr(part, ID_nondet_symbol))
    {
      // Replicating an expression repeats its literal X/Z positions but
      // leaves all occurrences of the same source wire connected.
      const auto count = numeric_cast_v<std::size_t>(rep.times());
      exprt::operandst parts;
      parts.reserve(count);
      for(std::size_t i = 0; i < count; ++i)
        parts.push_back(i == 0 ? part : verilog_sample_drive(rep.op(), fresh));
      return concatenation_exprt{std::move(parts), type};
    }
    return replication_exprt{rep.times(), std::move(part), type};
  }
  if(is_tristate_switch(e.id()))
  {
    // Without a co-driver a released switch has an unconstrained sample.
    const bool inverted = e.id() == ID_notif0 || e.id() == ID_notif1;
    const bool active_low = e.id() == ID_notif0 || e.id() == ID_bufif0 ||
                            e.id() == ID_pmos || e.id() == ID_rpmos;
    const unsignedbv_typet word{width};
    auto data = typecast_exprt::conditional_cast(
      verilog_sample_drive(e.operands()[0], fresh), word);
    auto enable = typecast_exprt::conditional_cast(
      verilog_sample_drive(e.operands()[1], fresh), word);
    if(inverted)
      data = bitnot_exprt{std::move(data)};
    if(active_low)
      enable = bitnot_exprt{std::move(enable)};
    if(e.id() == ID_cmos || e.id() == ID_rcmos)
      enable = bitor_exprt{
        std::move(enable),
        bitnot_exprt{typecast_exprt::conditional_cast(
          verilog_sample_drive(e.operands()[2], fresh), word)}};
    return typecast_exprt::conditional_cast(
      bitor_exprt{
        bitand_exprt{data, enable},
        bitand_exprt{fresh(word), bitnot_exprt{enable}}},
      type);
  }
  if(
    e.id() == ID_typecast || e.id() == ID_concatenation ||
    e.id() == ID_member || e.id() == ID_index || e.id() == ID_extractbit ||
    e.id() == ID_extractbits || e.id() == ID_verilog_bit_select ||
    e.id() == ID_verilog_non_indexed_part_select ||
    e.id() == ID_verilog_indexed_part_select_plus ||
    e.id() == ID_verilog_indexed_part_select_minus)
  {
    exprt result = e;
    if(e.id() == ID_concatenation)
      for(auto &op : result.operands())
        op = verilog_sample_drive(op, fresh);
    else
      result.operands().front() =
        verilog_sample_drive(e.operands().front(), fresh);
    result.type() = type;
    return result;
  }
  return e;
}

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

  // Only --undriven const populates these ranges during synthesis.
  // Leave the initial value unconstrained and hold it across transitions.
  // The physical drive of the corresponding bits remains high impedance.
  for(const auto &[id, ranges] : rtl.undriven)
  {
    const auto &net = symbols.lookup_ref(id);
    const auto visible = to_bitvector(lower(net.symbol_expr()));
    for(const auto &range : ranges)
    {
      const unsignedbv_typet type{numeric_cast_v<std::size_t>(range.width())};
      auto held =
        auxiliary(net, "undriven_" + integer2string(range.lower), type, true);
      auto next = held;
      next.id(ID_next_symbol);
      trans.trans().add_to_operands(equal_exprt{next, held});
      trans.invar().add_to_operands(equal_exprt{
        extractbits_exprt{
          visible, from_integer(range.lower, integer_typet{}), type},
        held});
    }
  }

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
    if(!rtl.merging_nets.count(entry.first))
      continue;
    const auto kind = net.type.get("#net_kind");
    if(
      entry.second.empty() && !connected.count(entry.first) &&
      (kind == ID_tri0 || kind == ID_tri1))
    {
      const bv_typet bits{width};
      auto value =
        kind == ID_tri1 ? bits.all_ones_expr() : bits.all_zeros_expr();
      nets.emplace(entry.first, pack(value, bits.all_zeros_expr()));
      trans.invar().add_to_operands(
        equal_exprt{net.symbol_expr(), from_bits(value, net.type)});
    }
    else
      nets.emplace(entry.first, auxiliary(net, "drive", encoded_type(width)));
  }

  // A single driver can feed a tristate bus through ports and selects.
  // Recover its physical drive on demand without allocating a pair of
  // signal planes for every public alias in the intervening hierarchy.
  std::function<exprt(const irep_idt &, bool)> lookup;
  const drive_lookupt physical_input = [&](const irep_idt &id)
  { return lookup(id, true); };
  const drive_lookupt logic_input = [&](const irep_idt &id) -> exprt
  {
    return rtl.literal_nets.count(id) ? lookup(id, false)
                                      : static_cast<exprt>(nil_exprt{});
  };
  const driver_valuet driver(physical_input, lower),
    logic_driver(logic_input, lower);
  const namespacet ns{symbols};
  encoding_mapt logic_nets;
  std::unordered_map<exprt, exprt, irep_hash> masks;
  std::set<std::pair<irep_idt, bool>> active;
  // With no released driver, disagreement or an X makes a bit free. This
  // consensus only uses two-state words and never constrains the drivers.
  auto consensus = [&](const irep_idt &id, bool physical)
  {
    const auto &net = symbols.lookup_ref(id);
    const auto width = width_of(net.symbol_expr());
    const bv_typet word{width};
    exprt zeros = word.all_zeros_expr(), ones = zeros, unknown = zeros,
          coverage = zeros;
    for(const auto &d : rtl.resolved_nets.at(id))
    {
      const bv_typet field{numeric_cast_v<std::size_t>(d.slice.width())};
      exprt a, b;
      if(has_subexpr(
           d.value,
           [&](const exprt &v)
           {
             return is_verilog_unknown_literal(v) ||
                    (v.id() == ID_symbol &&
                     rtl.resolved_nets.count(
                       to_symbol_expr(v).get_identifier()));
           }))
      {
        const auto &evaluate = physical ? driver : logic_driver;
        auto raw = evaluate(d.value, field.get_width());
        a = aval(raw);
        b = bval(raw);
      }
      else
      {
        a =
          typecast_exprt::conditional_cast(to_bitvector(lower(d.value)), field);
        b = field.all_zeros_expr();
      }
      coverage = bitor_exprt{
        std::move(coverage), place_bits(field.all_ones_expr(), d.slice, width)};
      ones = bitor_exprt{std::move(ones), place_bits(a, d.slice, width)};
      zeros = bitor_exprt{
        std::move(zeros), place_bits(bitnot_exprt{a}, d.slice, width)};
      unknown = bitor_exprt{std::move(unknown), place_bits(b, d.slice, width)};
    }
    auto gaps = simplify_expr(bitnot_exprt{std::move(coverage)}, ns);
    unknown = simplify_expr(
      bitor_exprt{std::move(unknown), bitand_exprt{zeros, ones}}, ns);
    if(physical)
      unknown = simplify_expr(bitor_exprt{unknown, gaps}, ns);
    auto gap_data = physical ? gaps
                             : static_cast<exprt>(bitand_exprt{
                                 typecast_exprt::conditional_cast(
                                   to_bitvector(net.symbol_expr()), word),
                                 gaps});
    auto data = simplify_expr(
      bitor_exprt{
        bitand_exprt{std::move(ones), bitnot_exprt{unknown}}, gap_data},
      ns);
    return std::make_pair(std::move(data), std::move(unknown));
  };
  auto constrain_sample =
    [&](const symbolt &net, const exprt &known_ones, const exprt &unknown)
  {
    const auto visible = net.symbol_expr();
    const bv_typet word{width_of(visible)};
    if(unknown == word.all_ones_expr())
      return; // The public wire itself is the free value for this frame.
    if(unknown == word.all_zeros_expr())
    {
      trans.invar().add_to_operands(
        equal_exprt{visible, from_bits(known_ones, visible.type())});
      return;
    }
    // The value of each unknown bit is the public bit itself. Constrain
    // only the known positions; an additional word of choices is redundant.
    auto observed =
      typecast_exprt::conditional_cast(to_bitvector(visible), word);
    auto different = bitxor_exprt{std::move(observed), known_ones};
    trans.invar().add_to_operands(simplify_expr(
      equal_exprt{
        bitand_exprt{std::move(different), bitnot_exprt{unknown}},
        word.all_zeros_expr()},
      ns));
  };
  lookup = [&](const irep_idt &id, bool physical) -> exprt
  {
    if(physical && !rtl.may_release.count(id) && !rtl.merging_nets.count(id))
    {
      // A fully driven ordinary source supplies its own computed value.
      // An open input further upstream cannot release this connection.
      if(rtl.literal_nets.count(id) || rtl.binary_conflicts.count(id))
        return lookup(id, false);
      return nil_exprt{};
    }
    auto &cache = physical || rtl.merging_nets.count(id) ? nets : logic_nets;
    auto found = cache.find(id);
    if(found != cache.end())
      return found->second;
    auto source = rtl.resolved_nets.find(id);
    if(source == rtl.resolved_nets.end())
      return nil_exprt{};
    const auto &net = symbols.lookup_ref(id);
    const auto width = width_of(net.symbol_expr());
    if(!active.emplace(id, physical).second)
    {
      // Only a cycle needs an equation to tie the recursive drive.
      auto anchor = auxiliary(
        net, physical ? "feedback_drive" : "feedback", encoded_type(width));
      cache.emplace(id, anchor);
      return anchor;
    }
    exprt value;
    if(rtl.binary_conflicts.count(id))
    {
      auto [data, unknown] = consensus(id, physical);
      value = pack(std::move(data), std::move(unknown));
    }
    else
    {
      // The remaining source slices are disjoint, including partial ports.
      // Assemble them in bit order and leave each gap released.
      const auto &evaluate = physical ? driver : logic_driver;
      std::map<verilog_rtl_slicet, exprt> slices;
      for(const auto &d : source->second)
        slices.emplace(
          d.slice,
          evaluate(d.value, numeric_cast_v<std::size_t>(d.slice.width())));
      auto gap = [&](std::size_t lo, std::size_t hi)
      {
        const bv_typet field{hi - lo};
        if(physical)
          return high_z(hi - lo);
        // In ordinary logic an unconnected position is a sampled public
        // input, even when a different position of the vector has a literal X.
        return pack(
          extractbits_exprt{to_bitvector(net.symbol_expr()), lo, field},
          field.all_zeros_expr());
      };
      std::size_t upper = width;
      exprt::operandst parts;
      for(auto it = slices.rbegin(); it != slices.rend(); ++it)
      {
        const auto end = numeric_cast_v<std::size_t>(it->first.higher) + 1;
        if(upper > end)
          parts.push_back(gap(end, upper));
        parts.push_back(it->second);
        upper = numeric_cast_v<std::size_t>(it->first.lower);
      }
      if(upper > 0)
        parts.push_back(gap(0, upper));
      value = parts.size() == 1
                ? parts.front()
                : aval_bval_concatenation(parts, encoded_type(width));
      value = simplify_expr(std::move(value), ns);
    }
    const bv_typet word{width};
    value.type() = encoded_type(width);
    auto unknown = simplify_expr(bval(value), ns);
    auto data = simplify_expr(aval(value), ns);
    // Materialize only dynamic masks needed by a downstream drive. The
    // public signal already contains the data on every known bit.
    auto share_mask = [&](exprt mask, const char *suffix)
    {
      if(mask.is_constant() || mask.id() == ID_symbol)
        return mask;
      auto existing = masks.find(mask);
      if(existing != masks.end())
        return existing->second;
      auto shared = auxiliary(
        net,
        physical ? std::string("physical_") + suffix : std::string(suffix),
        word);
      masks.emplace(mask, shared);
      trans.invar().add_to_operands(equal_exprt{shared, mask});
      return static_cast<exprt>(shared);
    };
    exprt released = word.all_zeros_expr();
    if(rtl.may_release.count(id))
      released =
        share_mask(simplify_expr(bitand_exprt{data, unknown}, ns), "released");
    unknown = share_mask(std::move(unknown), "unknown");
    if(
      !physical && rtl.ambiguous_drives.count(id) &&
      source->second.size() == 1 &&
      source->second.front().slice.width() == width &&
      !has_subexpr(
        source->second.front().value,
        [](const exprt &v)
        {
          return v.id() == ID_verilog_case_equality ||
                 v.id() == ID_verilog_case_inequality;
        }))
    {
      // On each known output bit the native operation on public input
      // samples has the IEEE value. Its data path needs no mask logic.
      auto zero = [](const typet &type) -> exprt
      { return from_integer(0, type); };
      auto projected =
        lower(verilog_sample_drive(source->second.front().value, zero));
      constrain_sample(
        net,
        typecast_exprt::conditional_cast(to_bitvector(projected), word),
        unknown);
    }
    else if(
      !physical &&
      (rtl.ambiguous_drives.count(id) || rtl.binary_conflicts.count(id)))
      constrain_sample(net, data, unknown);
    auto visible =
      typecast_exprt::conditional_cast(to_bitvector(net.symbol_expr()), word);
    auto public_data = simplify_expr(
      bitor_exprt{
        std::move(released),
        bitand_exprt{std::move(visible), bitnot_exprt{unknown}}},
      ns);
    value = pack(public_data, unknown);
    active.erase({id, physical});
    auto inserted = cache.emplace(id, value);
    if(!inserted.second)
      trans.invar().add_to_operands(equal_exprt{inserted.first->second, value});
    return inserted.first->second;
  };
  std::map<irep_idt, net_valuet> local;
  for(const auto &entry : rtl.resolved_nets)
  {
    if(
      !rtl.merging_nets.count(entry.first) ||
      nets.at(entry.first).id() != ID_symbol)
      continue;
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
    auto drive = simplify_expr(resolve(drives, width), ns);
    drive.type() = encoded_type(width);
    if(connected.count(entry.first) && !drives.empty() && !drive.is_constant())
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

    // The public wire itself supplies the unknown bits for this frame.
    // A direct connection shares the source's sample on those bits.
    const bool alias = !connected.count(entry.first) &&
                       entry.second.size() == 1 &&
                       entry.second.front().slice.width() == width &&
                       is_verilog_net_connection(entry.second.front().value);
    exprt choice =
      alias ? lower(entry.second.front().value) : static_cast<exprt>(visible);
    local.emplace(
      entry.first,
      net_valuet{
        std::move(drive),
        std::move(fallback),
        typecast_exprt{to_bitvector(choice), bits}});
  }

  for(const auto &id : rtl.binary_conflicts)
    lookup(id, false);
  for(const auto &id : rtl.ambiguous_drives)
    lookup(id, false);

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
    drive = simplify_expr(std::move(drive), ns);
    drive.type() = raw.type();
    trans.invar().add_to_operands(equal_exprt{raw, drive});
    // Keep constant planes visible after defining the shared bus signal.
    auto mask = simplify_expr(bval(drive), ns);
    if(!mask.is_constant())
      mask = bval(raw);
    auto data = simplify_expr(aval(drive), ns);
    if(!data.is_constant())
      data = aval(raw);
    const auto public_bits =
      typecast_exprt{to_bitvector(visible), bv_typet{width}};
    if(choice == public_bits)
    {
      constrain_sample(symbols.lookup_ref(entry.first), data, mask);
      continue;
    }
    auto read = bitor_exprt{
      bitand_exprt{data, bitnot_exprt{mask}},
      bitand_exprt{std::move(choice), mask}};
    auto projected = from_bits(std::move(read), visible.type());
    trans.invar().add_to_operands(equal_exprt{visible, std::move(projected)});
  }
}
