
#include <util/arith_tools.h>
#include <util/bitvector_types.h>

#include <ebmc/instrument_past.h>
#include <testing-utils/use_catch.h>
#include <trans-word-level/instantiate_word_level.h>
#include <verilog/verilog_expr.h>

TEST_CASE("Instantiate reads of packed history defaults", "[word-level]")
{
  const auto integer = [](int value) {
    return from_integer(value, integer_typet{});
  };
  array_typet lanes{signedbv_typet{4}, integer(3)};
  lanes.set(ID_C_verilog_type, ID_verilog_packed_array);
  array_typet rows{lanes, integer(2)};
  rows.set(ID_C_verilog_type, ID_verilog_packed_array);

  const verilog_past_exprt past{symbol_exprt{"v", rows}, integer(2)};
  const index_exprt row{past, integer(1)};
  const index_exprt lane{row, integer(2)};

  CHECK(instantiate(lane, 0, 3) == from_integer(0, lanes.element_type()));
  CHECK(
    instantiate(lane, 2, 3) ==
    index_exprt(index_exprt(symbol_exprt{"v@0", rows}, integer(1)), integer(2)));

  const auto check_retained_read = [&](const exprt &index) {
    CHECK(
      instantiate(index_exprt{row, index}, 0, 3) ==
      index_exprt(to_array_of_expr(past.default_value()).what(), index));
  };
  check_retained_read(integer(-1));
  check_retained_read(integer(3));
  // A wider index that would become an in-range index if truncated.
  check_retained_read(
    from_integer(string2integer("18446744073709551616"), unsignedbv_typet{65}));

  const auto unknown_lane = symbol_exprt{"i", unsignedbv_typet{2}};
  CHECK(
    instantiate(index_exprt{row, unknown_lane}, 0, 3) ==
    index_exprt(
      to_array_of_expr(past.default_value()).what(),
      symbol_exprt{"i@0", unknown_lane.type()}));

  // An unpacked array remains an array even when read at a fixed index.
  const array_typet unpacked{lanes.element_type(), integer(3)};
  const array_of_exprt unpacked_default{
    from_integer(0, unpacked.element_type()), unpacked};
  const index_exprt unpacked_read{unpacked_default, integer(2)};
  CHECK(instantiate(unpacked_read, 0, 3) == unpacked_read);

  transition_systemt system;
  symbolt module{"main", typet{ID_module}, ID_Verilog};
  module.module = "main";
  CHECK_FALSE(system.symbol_table.add(std::move(module)));
  system.main_symbol = &system.symbol_table.lookup_ref("main");
  ebmc_propertiest properties;
  auto &property = properties.properties.emplace_back();
  const auto check_history_type = [&](const typet &type) {
    REQUIRE(system.symbol_table.symbols.size() == 3);
    for(const auto &entry : system.symbol_table.symbols)
      if(entry.second.is_state_var)
        CHECK(entry.second.type == type);
  };

  SECTION("Fixed lanes initialize scalar history state")
  {
    property.normalized_expr =
      equal_exprt{lane, from_integer(0, lane.type())};
    instrument_past(system, properties);
    check_history_type(lane.type());
    CHECK(to_equal_expr(property.normalized_expr).lhs().id() == ID_symbol);
  }

  SECTION("Unchecked reads still read the original aggregate history state")
  {
    const index_exprt unknown_read{row, unknown_lane};
    property.normalized_expr =
      equal_exprt{unknown_read, from_integer(0, unknown_read.type())};
    instrument_past(system, properties);
    check_history_type(rows);
    const auto &read =
      to_index_expr(to_equal_expr(property.normalized_expr).lhs());
    CHECK(read.index() == unknown_lane);
  }
}
