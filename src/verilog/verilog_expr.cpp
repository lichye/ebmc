/*******************************************************************\

Module: Verilog Expressions

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "verilog_expr.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/expr_iterator.h>
#include <util/mathematical_types.h>
#include <util/prefix.h>

#include "aval_bval_encoding.h"
#include "verilog_initializer.h"
#include "verilog_typecheck_base.h"
#include "verilog_types.h"

#include <algorithm>

verilog_wildcard_equality_exprt::verilog_wildcard_equality_exprt(
  exprt lhs,
  exprt rhs)
  : binary_exprt(
      std::move(lhs),
      ID_verilog_wildcard_equality,
      std::move(rhs),
      verilog_unsignedbv_typet{1})
{
}

typet verilog_declaratort::merged_type(const typet &declaration_type) const
{
  typet result = type();
  typet *p = &result;

  while(p->id() == ID_verilog_unpacked_array ||
        p->id() == ID_verilog_associative_array ||
        p->id() == ID_verilog_queue || p->id() == ID_verilog_dynamic_array)
  {
    p = &to_type_with_subtype(*p).subtype();
  }

  DATA_INVARIANT(
    p->is_nil(),
    "merged_type only works on unpacked, associative and dynamic arrays and "
    "queues");

  *p = declaration_type;

  return result;
}

static bool is_system_function_identifier(const exprt &function)
{
  return function.id() == ID_verilog_identifier &&
         has_prefix(
           id2string(to_verilog_identifier_expr(function).base_name()), "$");
}

bool function_call_exprt::is_system_function_call() const
{
  return is_system_function_identifier(function());
}

bool verilog_function_callt::is_system_function_call() const
{
  return is_system_function_identifier(function());
}

void verilog_module_sourcet::show(std::ostream &out) const
{
  out << "Module: " << base_name() << '\n';

  out << "  Parameter ports:\n";

  for(auto &parameter : parameter_port_decls())
    out << "    " << parameter.pretty() << '\n';

  out << '\n';

  out << "  Ports:\n";

  for(auto &port : ports())
    out << "    " << port.pretty() << '\n';

  out << '\n';

  out << "  Module items:\n";

  for(auto &item : module_items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

void verilog_checkert::show(std::ostream &out) const
{
  out << "Checker: " << base_name() << '\n';

  out << "  Ports:\n";

  for(auto &port : ports())
    out << "    " << port.pretty() << '\n';

  out << '\n';

  out << "  Items:\n";

  for(auto &item : items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

void verilog_packaget::show(std::ostream &out) const
{
  out << "Package: " << base_name() << '\n';

  out << "  Items:\n";

  for(auto &item : items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

void verilog_programt::show(std::ostream &out) const
{
  out << "Program: " << base_name() << '\n';

  out << "  Items:\n";

  for(auto &item : items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

void verilog_classt::show(std::ostream &out) const
{
  out << "Class: " << base_name() << '\n';

  out << "  Items:\n";

  for(auto &item : items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

void verilog_interfacet::show(std::ostream &out) const
{
  out << "Interface: " << base_name() << '\n';

  out << "  Items:\n";

  for(auto &item : items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

void verilog_udpt::show(std::ostream &out) const
{
  out << "UDP: " << base_name() << '\n';

  out << "  Items:\n";

  for(auto &item : items())
    out << "    " << item.pretty() << '\n';

  out << '\n';
}

static mp_integer part_select_src_width(const typet &t)
{
  if(t.id() == ID_bool)
    return 1;

  if(
    t.id() == ID_bv || t.id() == ID_unsignedbv || t.id() == ID_signedbv ||
    t.id() == ID_verilog_signedbv || t.id() == ID_verilog_unsignedbv)
  {
    return to_bitvector_type(t).get_width();
  }

  PRECONDITION(false);
}

/// Extract the internal bits [bottom, bottom+width-1] from \p src, where
/// \p width is the width of \p type. Bits outside the range of \p src are
/// padded (1800-2017 11.5.1: out-of-bounds bit-select is x for 4-state and
/// 0 for 2-state values).
static exprt part_select_extract(
  exprt src,
  mp_integer bottom,
  const typet &type,
  const source_locationt &source_location)
{
  auto get_width = part_select_src_width;

  mp_integer top = bottom + get_width(type) - 1;

  if(bottom < 0)
  {
    // Pad below (at the LSB internal end); this shifts the internal bit
    // positions up, so bottom/top are adjusted accordingly.
    auto padding_width = -bottom;
    auto padding = from_integer(
      0, unsignedbv_typet{numeric_cast_v<std::size_t>(padding_width)});
    auto new_type = unsignedbv_typet{
      numeric_cast_v<std::size_t>(get_width(src.type()) + padding_width)};
    src = concatenation_exprt(std::move(src), padding, new_type);
    bottom += padding_width;
    top += padding_width;
  }

  if(top >= get_width(src.type()))
  {
    // Pad above (at the MSB internal end); this does not change the
    // internal bit positions.
    auto padding_width = top - get_width(src.type()) + 1;
    auto padding = from_integer(
      0, unsignedbv_typet{numeric_cast_v<std::size_t>(padding_width)});
    auto new_type = unsignedbv_typet{
      numeric_cast_v<std::size_t>(get_width(src.type()) + padding_width)};
    src = concatenation_exprt(padding, std::move(src), new_type);
  }

  // A Boolean result is a predicate, including a one-bit packed member
  // read through goto-vv. extractbits is only defined for bitvector results.
  if(type.id() == ID_bool)
  {
    if(src.type().id() == ID_bool)
      return src;
    return extractbit_exprt{std::move(src), from_integer(bottom, integer_typet{})}
      .with_source_location(source_location);
  }

  return extractbits_exprt{
    std::move(src), from_integer(bottom, integer_typet{}), type}
    .with_source_location(source_location);
}

static exprt lower(const verilog_non_indexed_part_select_exprt &part_select)
{
  auto get_width = part_select_src_width;

  auto &src = part_select.src();

  auto op1 = numeric_cast_v<mp_integer>(to_constant_expr(part_select.msb()));
  auto op2 = numeric_cast_v<mp_integer>(to_constant_expr(part_select.lsb()));

  mp_integer src_width = get_width(src.type());
  auto &src_vector_type = to_verilog_vector_type(src.type());
  auto src_offset = src_vector_type.offset();
  bool increasing = src_vector_type.increasing();

  // op1/op2 are the declared indices, normalised so that op1>=op2.
  //
  // We map the declared range op2..op1 to a contiguous range of internal
  // (extractbits) bits starting at bottom. For a decreasing range [msb:lsb]
  // (msb>=lsb) the declared index i maps to internal bit i-offset. For an
  // increasing range [lsb:msb] (lsb<=msb) the declared index offset is the
  // most significant bit, so index i maps to internal bit
  // (width-1)-(i-offset) (1800-2017 7.4.1, 11.5.1). Hence for an increasing
  // range the numerically larger declared index op1 maps to the lowest
  // internal bit.
  mp_integer bottom;

  if(increasing)
    bottom = (src_width - 1) - (op1 - src_offset);
  else
    bottom = op2 - src_offset;

  return part_select_extract(
    src, bottom, part_select.type(), part_select.source_location());
}

exprt verilog_non_indexed_part_select_exprt::lower() const
{
  return ::lower(*this);
}

static exprt encoded_vector(exprt value)
{
  if(is_four_valued(value))
  {
    if(value.is_constant())
      return lower_to_aval_bval(to_constant_expr(value));
    value.type() = lower_to_aval_bval(value.type());
  }
  else if(value.type().id() == ID_bool)
    return typecast_exprt{std::move(value), unsignedbv_typet{1}};
  return value;
}

/// Run the same zero-padded selection on value bits and on the complement
/// of the unknown-bit plane. A padded bit then has (aval, bval) = (0, 1).
template <typename Select>
static exprt select_vector(
  const exprt &src,
  const typet &type,
  Select select,
  bool in_bounds = false)
{
  if(!is_four_valued(type))
    return select(is_aval_bval(src) ? aval(src) : src, type);

  const auto plane_type = bv_typet{to_bitvector_type(type).get_width()};
  exprt value = select(aval(src), plane_type);
  exprt unknown;
  if(in_bounds)
    unknown = is_aval_bval(src) ? select(bval(src), plane_type)
                                : exprt{plane_type.all_zeros_expr()};
  else
    unknown = bitnot_exprt{select(bitnot_exprt{bval(src)}, plane_type)};
  return concatenation_exprt{
    {std::move(unknown), std::move(value)}, lower_to_aval_bval(type)};
}

static exprt
lower(const verilog_indexed_part_select_plus_or_minus_exprt &part_select)
{
  const auto &raw_src = part_select.src();
  auto src = encoded_vector(raw_src);
  const mp_integer src_width = is_aval_bval(src)
                                 ? aval_bval_width(src.type())
                                 : part_select_src_width(src.type());
  const auto &src_vector_type = to_verilog_vector_type(raw_src.type());
  const auto src_offset =
    part_select.get(ID_C_offset).empty()
      ? src_vector_type.offset()
      : string2integer(part_select.get_string(ID_C_offset));
  const bool increasing =
    part_select.get_bool(ID_C_increasing) || src_vector_type.increasing();
  const bool plus = part_select.id() == ID_verilog_indexed_part_select_plus;
  const auto &type = part_select.type();
  const auto width = part_select_src_width(type);
  const auto index = encoded_vector(part_select.index());

  // Map the SystemVerilog base to the lowest internal bit of the slice.
  mp_integer bias = src_offset;
  if(increasing)
    bias += plus ? src_width - width : src_width - 1;
  else if(!plus)
    bias += width - 1;

  if(index.is_constant())
  {
    const auto &constant = to_constant_expr(index);
    auto missing = [&]() -> exprt
    {
      if(is_four_valued(type))
        return lower_to_aval_bval(constant_exprt{
          std::string(numeric_cast_v<std::size_t>(width), 'x'), type});
      return from_integer(0, type);
    };
    if(is_aval_bval(index) && aval_bval_has_xz(constant))
      return missing();
    const auto base =
      is_aval_bval(index)
        ? bvrep2integer(
            constant.get_value(),
            aval_bval_width(index.type()),
            aval_bval_underlying(index.type()).id() == ID_signedbv)
        : numeric_cast_v<mp_integer>(constant);
    const mp_integer bottom = increasing ? bias - base : base - bias;
    if(bottom >= src_width || bottom + width <= 0)
      return missing();
    return select_vector(
      src,
      type,
      [&](exprt plane, const typet &plane_type)
      {
        return part_select_extract(
          std::move(plane), bottom, plane_type, part_select.source_location());
      },
      bottom >= 0 && bottom + width <= src_width);
  }

  exprt numeric_index =
    is_aval_bval(index)
      ? typecast_exprt{aval(index), aval_bval_underlying(index.type())}
      : index;
  // Bits below internal index zero need at most width-1 bits of padding.
  // Omit the low padding when the adjusted index is always nonnegative.
  const bool unbounded_index = numeric_index.type().id() == ID_integer;
  const mp_integer low_padding = increasing || bias > 0 ||
                                     numeric_index.type().id() == ID_signedbv ||
                                     unbounded_index
                                   ? width - 1
                                   : mp_integer{0};
  const mp_integer extended_width = src_width + low_padding;
  const mp_integer padded_width = std::max(extended_width, width);
  const mp_integer adjustment =
    increasing ? bias + low_padding : low_padding - bias;
  const mp_integer bound =
    padded_width + (adjustment < 0 ? -adjustment : adjustment);
  typet distance_type = integer_typet{};
  if(!unbounded_index)
  {
    const auto index_width =
      to_bitvector_type(numeric_index.type()).get_width();
    distance_type = signedbv_typet{std::max(
      index_width + 2, numeric_cast_v<std::size_t>(address_bits(bound)) + 2)};
  }
  auto wide_index =
    typecast_exprt::conditional_cast(std::move(numeric_index), distance_type);
  auto delta = from_integer(adjustment, distance_type);
  exprt distance = increasing ? exprt{minus_exprt{delta, wide_index}}
                              : plus_exprt{wide_index, delta};
  auto in_range = and_exprt{
    greater_than_or_equal_exprt{distance, from_integer(0, distance_type)},
    less_than_exprt{distance, from_integer(padded_width, distance_type)}};
  auto known_in_range =
    and_exprt{not_exprt{has_xz(index)}, std::move(in_range)};
  const auto padded_bits = numeric_cast_v<std::size_t>(padded_width);
  const unsignedbv_typet shift_type{
    numeric_cast_v<std::size_t>(address_bits(padded_width))};
  const auto shift_distance = typecast_exprt{distance, shift_type};

  return select_vector(
    src,
    type,
    [&](exprt plane, const typet &plane_type)
    {
      if(low_padding != 0)
      {
        const bv_typet pad{numeric_cast_v<std::size_t>(low_padding)};
        plane = concatenation_exprt{
          {std::move(plane), pad.all_zeros_expr()},
          bv_typet{numeric_cast_v<std::size_t>(extended_width)}};
      }
      if(extended_width < padded_width)
      {
        const bv_typet pad{
          numeric_cast_v<std::size_t>(padded_width - extended_width)};
        plane = concatenation_exprt{
          {pad.all_zeros_expr(), std::move(plane)}, bv_typet{padded_bits}};
      }

      auto selected = extractbits_exprt{
        lshr_exprt{std::move(plane), shift_distance}, 0, plane_type};
      auto fill = bv_typet{numeric_cast_v<std::size_t>(width)}.all_zeros_expr();
      fill.type() = plane_type;
      return if_exprt{known_in_range, std::move(selected), std::move(fill)}
        .with_source_location(part_select.source_location());
    });
}

exprt verilog_indexed_part_select_plus_or_minus_exprt::lower() const
{
  return ::lower(*this);
}

exprt verilog_streaming_concatenation_exprt::lower() const
{
  // The stream_concatenation is a braced list of one or more
  // stream_expressions (1800-2017 11.4.14). We first pack these into a
  // single bit vector, preserving the written (left-to-right) order, which
  // matches the semantics of an ordinary concatenation.
  auto pack = [](const exprt::operandst &stream_expressions) -> exprt
  {
    PRECONDITION(!stream_expressions.empty());

    if(stream_expressions.size() == 1)
      return stream_expressions.front();

    mp_integer width = 0;
    for(auto &op : stream_expressions)
      width += to_bitvector_type(op.type()).get_width();

    return concatenation_exprt{
      stream_expressions, unsignedbv_typet{numeric_cast_v<std::size_t>(width)}};
  };

  if(id() == ID_verilog_streaming_concatenation_left_to_right)
  {
    // The slice size does not matter; the packed stream is the result.
    return pack(stream_expressions());
  }
  else if(id() == ID_verilog_streaming_concatenation_right_to_left)
  {
    auto packed = pack(stream_expressions());

    if(packed.type().id() == ID_bool)
      return packed;

    // The packed stream is reversed in blocks of slice_size bits (default 1).
    auto slice_size_int =
      has_slice_size()
        ? numeric_cast_v<std::size_t>(to_constant_expr(slice_size()))
        : std::size_t(1);
    if(slice_size_int == 1)
      return bitreverse_exprt{std::move(packed)};
    else
      return bswap_exprt{std::move(packed), slice_size_int};
  }
  else
    PRECONDITION(false);
}

exprt verilog_inside_exprt::lower() const
{
  exprt::operandst disjuncts;

  for(auto &range : range_list())
  {
    if(range.id() == ID_verilog_value_range)
    {
      auto &range_expr = to_verilog_value_range_expr(range);
      DATA_INVARIANT(
        op().type() == range_expr.lhs().type(),
        "inside_exprt type consistency");
      DATA_INVARIANT(
        op().type() == range_expr.rhs().type(),
        "inside_exprt type consistency");
      disjuncts.push_back(and_exprt{
        binary_relation_exprt{op(), ID_ge, range_expr.lhs()},
        binary_relation_exprt{op(), ID_le, range_expr.rhs()}});
    }
    else
    {
      DATA_INVARIANT(
        op().type() == range.type(), "inside_exprt type consistency");
      auto &range_type = range.type();
      if(
        range_type.id() == ID_verilog_unsignedbv ||
        range_type.id() == ID_verilog_signedbv)
      {
        disjuncts.push_back(typecast_exprt{
          verilog_wildcard_equality_exprt{op(), range}, bool_typet()});
      }
      else
        disjuncts.push_back(equal_exprt{op(), range});
    }
  }

  return disjunction(disjuncts);
}

exprt verilog_past_exprt::default_value() const
{
  auto value_opt = verilog_default_initializer(type());
  CHECK_RETURN(value_opt.has_value());
  return *value_opt;
}

bool verilog_fort::has_scope() const
{
  return std::any_of(
    initialization().begin(),
    initialization().end(),
    [](const verilog_statementt &s) { return s.id() == ID_decl; });
}

constant_exprt verilog_unbased_unsized_literal_exprt::expand() const
{
  auto &type = this->type();
  auto &value = this->value();
  PRECONDITION(
    type.id() == ID_unsignedbv || type.id() == ID_verilog_unsignedbv);

  if(value == ID_0)
    return to_unsignedbv_type(type).zero_expr();
  else if(value == ID_1)
    return to_unsignedbv_type(type).largest_expr();
  else if(value == ID_x)
    return to_verilog_unsignedbv_type(type).all_x_expr();
  else if(value == ID_z)
    return to_verilog_unsignedbv_type(type).all_z_expr();
  else
    PRECONDITION(false);
}

verilog_unbased_unsized_literal_exprt
verilog_unbased_unsized_literal_exprt::with_context(std::size_t width) const
{
  verilog_unbased_unsized_literal_exprt copy(*this);
  auto &type = copy.type();
  PRECONDITION(
    type.id() == ID_unsignedbv || type.id() == ID_verilog_unsignedbv);

  if(type.id() == ID_unsignedbv)
    to_unsignedbv_type(type).set_width(width);
  else
    to_verilog_unsignedbv_type(type).set_width(width);

  return copy;
}

constant_exprt verilog_based_unsized_literal_exprt::expand() const
{
  return lower();
}

constant_exprt verilog_based_unsized_literal_exprt::lower() const
{
  auto &t = type();
  auto width = numeric_cast_v<std::size_t>(to_bitvector_type(t).get_width());
  auto bit_value = id2string(value());
  bool is_signed = t.id() == ID_signedbv || t.id() == ID_verilog_signedbv;

  // Extend to type width
  if(bit_value.size() < width)
  {
    auto self_determined = std::max(bit_value.size(), std::size_t(32));

    char ext;

    if(bit_value.empty())
      ext = '0';
    else
    {
      if(is_signed)
        ext = bit_value[0];
      else if(
        width > self_determined && (bit_value[0] == 'x' || bit_value[0] == 'z'))
      {
        // IEEE 1800-2017 5.7.1: context extension with x/z MSB
        ext = bit_value[0];
      }
      else
        ext = '0';
    }

    bit_value = std::string(width - bit_value.size(), ext) + bit_value;
  }

  if(t.id() == ID_unsignedbv || t.id() == ID_signedbv)
  {
    auto int_value = binary2integer(bit_value, t.id() == ID_signedbv);
    return constant_exprt{integer2bvrep(int_value, width), t};
  }
  else
  {
    return constant_exprt{bit_value, t};
  }
}

verilog_based_unsized_literal_exprt
verilog_based_unsized_literal_exprt::with_context(std::size_t width) const
{
  auto type = this->type();

  if(type.id() == ID_unsignedbv)
    to_unsignedbv_type(type).set_width(width);
  else if(type.id() == ID_verilog_unsignedbv)
    to_verilog_unsignedbv_type(type).set_width(width);
  else if(type.id() == ID_signedbv)
    to_signedbv_type(type).set_width(width);
  else
    to_verilog_signedbv_type(type).set_width(width);

  return verilog_based_unsized_literal_exprt{value(), type};
}

verilog_module_instancet::verilog_module_instancet(
  const irep_idt _module_identifier)
  : nullary_exprt{ID_verilog_module_instance, verilog_module_instance_typet{}}
{
  module_identifier(_module_identifier);
}

verilog_set_genvarst::genvarst verilog_set_genvarst::build_map() const
{
  const auto &variables = this->variables();
  genvarst genvars;
  for(auto &var : variables)
    genvars[id2string(var.first)] = string2integer(var.second.id_string());
  return genvars;
}
