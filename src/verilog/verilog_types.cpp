/*******************************************************************\

Module: Verilog Types

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "verilog_types.h"

#include <util/arith_tools.h>
#include <util/mathematical_types.h>
#include <util/std_expr.h>

constant_exprt verilog_chandle_typet::null_expr() const
{
  return encoding().all_zeros_expr();
}

constant_exprt verilog_event_typet::null_expr() const
{
  return encoding().all_zeros_expr();
}

void verilog_package_scope_typet::check(
  const typet &type,
  const validation_modet vm)
{
  PRECONDITION(type.id() == ID_verilog_package_scope);
  type_with_subtypest::check(type);
  DATA_CHECK(
    vm,
    to_type_with_subtypes(type).subtypes().size() == 2,
    "verilog_package_scope type must have two subtypes");
}

verilog_array_typet::verilog_array_typet(
  irep_idt verilog_type,
  typet element_type,
  mp_integer size,
  mp_integer offset,
  bool increasing)
  : array_typet{std::move(element_type), from_integer(size, integer_typet{})}
{
  set(ID_C_verilog_type, verilog_type);
  set(ID_offset, integer2string(offset));
  set(ID_C_increasing, increasing);
}

mp_integer verilog_array_typet::size_int() const
{
  auto size_opt = numeric_cast<mp_integer>(size());
  PRECONDITION(size_opt.has_value());
  return size_opt.value();
}

mp_integer verilog_array_typet::offset() const
{
  auto &offset_irep = static_cast<const exprt &>(find(ID_offset));
  DATA_INVARIANT(offset_irep.is_not_nil(), "array must have offset");
  return string2integer(offset_irep.id_string());
}

typet verilog_array_typet::index_type() const
{
  auto base = offset();
  // Negative packed bounds still need enough bits for all zero-based slots.
  if(is_packed() && base < 0)
    base = 0;
  return unsignedbv_typet{address_bits(size_int() + base)};
}

constant_exprt verilog_unsignedbv_typet::all_x_expr() const
{
  return constant_exprt{std::string(get_width(), 'x'), *this};
}

constant_exprt verilog_unsignedbv_typet::all_z_expr() const
{
  return constant_exprt{std::string(get_width(), 'z'), *this};
}

bool is_interface_array_type(const typet &type)
{
  if(type.id() != ID_array)
    return false;

  auto *element_type = &to_array_type(type).element_type();
  while(element_type->id() == ID_array)
    element_type = &to_array_type(*element_type).element_type();

  return element_type->id() == ID_verilog_module_instance;
}

bool is_unbounded_array_type(const typet &type)
{
  if(
    type.id() == ID_verilog_dynamic_array || type.id() == ID_verilog_queue ||
    type.id() == ID_verilog_associative_array)
  {
    return true;
  }

  // e.g., an unpacked array of dynamic arrays
  if(type.id() == ID_array)
    return is_unbounded_array_type(to_array_type(type).element_type());

  return false;
}

bool is_four_state_vector(const typet &type)
{
  const auto element = type.get(ID_C_verilog_vector_type);
  const auto declared = type.get(ID_C_verilog_type);
  return type.id() == ID_verilog_signedbv ||
         type.id() == ID_verilog_unsignedbv || type.get_bool(ID_C_implicit) ||
         element == ID_verilog_logic || element == ID_reg ||
         element == ID_wire || declared == ID_verilog_logic ||
         declared == ID_wire || declared == ID_integer ||
         declared == ID_verilog_time;
}

typet make_two_valued(typet src)
{
  // Scalar enums still have a nominal type and integral cast semantics.
  const bool can_be_boolean = src.get(ID_C_verilog_type) != ID_verilog_enum;
  if(src.id() == ID_verilog_unsignedbv)
  {
    // Is it [0:0]? If so, make it 'bool'
    if(
      to_verilog_unsignedbv_type(src).width() == 1 &&
      src.get(ID_C_offset) == irep_idt{} && can_be_boolean)
    {
      auto result = bool_typet{}.with_source_location(src.source_location());
      irep_idt verilog_type = src.get(ID_C_verilog_type);
      if(verilog_type != irep_idt{})
        result.set(ID_C_verilog_type, verilog_type);
      irep_idt verilog_vector_type = src.get(ID_C_verilog_vector_type);
      if(verilog_vector_type != irep_idt{})
        result.set(ID_C_verilog_vector_type, verilog_vector_type);
      return result;
    }
    else
    {
      // replace id, preserve width and location, if any
      src.id(ID_unsignedbv);
      return src;
    }
  }
  else if(src.id() == ID_verilog_signedbv)
  {
    // replace id, preserve width and location, if any
    src.id(ID_signedbv);
    return src;
  }
  else if(src.id() == ID_unsignedbv)
  {
    // Is it [0:0]? If so, make it 'bool'
    if(
      to_unsignedbv_type(src).width() == 1 &&
      src.get(ID_C_offset) == irep_idt{} && can_be_boolean)
    {
      auto result = bool_typet{}.with_source_location(src.source_location());
      irep_idt verilog_type = src.get(ID_C_verilog_type);
      if(verilog_type != irep_idt{})
        result.set(ID_C_verilog_type, verilog_type);
      irep_idt verilog_vector_type = src.get(ID_C_verilog_vector_type);
      if(verilog_vector_type != irep_idt{})
        result.set(ID_C_verilog_vector_type, verilog_vector_type);
      return result;
    }
    else
    {
      // leave as is
      return src;
    }
  }
  else if(src.id() == ID_array)
  {
    auto &array_type = to_array_type(src);
    array_type.element_type() = make_two_valued(array_type.element_type());
    return src;
  }
  else if(src.id() == ID_struct)
  {
    for(auto &component : to_struct_type(src).components())
      component.type() = make_two_valued(component.type());
    return src;
  }
  else
    return src;
}

typet make_four_valued(typet src, bool declared_only)
{
  // The inverse of make_two_valued: turn a two-valued type into its
  // four-valued (x/z-capable) counterpart. Used for parameters whose
  // (constant) value contains x/z, which must be retained (1800-2017
  // 6.20.2); state variables continue to use the two-valued modelling.
  if(src.id() == ID_array)
  {
    auto &element = to_array_type(src).element_type();
    element = make_four_valued(element, declared_only);
  }
  else if(src.id() == ID_struct)
  {
    for(auto &component : to_struct_type(src).components())
      component.type() = make_four_valued(component.type(), declared_only);
  }
  else if(
    declared_only && (!is_four_state_vector(src) ||
                      src.get(ID_C_verilog_type) == ID_verilog_enum))
    return src;
  else if(src.id() == ID_bool)
  {
    auto result = verilog_unsignedbv_typet{1};
    result.add_source_location() = src.source_location();
    irep_idt verilog_type = src.get(ID_C_verilog_type);
    if(!verilog_type.empty())
      result.set(ID_C_verilog_type, verilog_type);
    irep_idt verilog_vector_type = src.get(ID_C_verilog_vector_type);
    if(!verilog_vector_type.empty())
      result.set(ID_C_verilog_vector_type, verilog_vector_type);
    return std::move(result);
  }
  else if(src.id() == ID_unsignedbv)
  {
    // replace id, preserve width/offset/markers and location
    src.id(ID_verilog_unsignedbv);
    return src;
  }
  else if(src.id() == ID_signedbv)
  {
    src.id(ID_verilog_signedbv);
    return src;
  }
  return src;
}
