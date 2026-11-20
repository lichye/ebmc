#include <ebmc/netlist.h>

#include <testing-utils/use_catch.h>
#include <trans-netlist/netlist.h>
#include <verilog/sva_expr.h>
#include <verilog/verilog_expr.h>

SCENARIO("Repeated netlist construction gives each history its own latch")
{
  transition_systemt system;
  symbolt module{"main", typet{ID_module}, ID_Verilog};
  module.module = module.name;
  REQUIRE_FALSE(system.symbol_table.add(module));
  system.main_symbol = &system.symbol_table.lookup_ref("main");

  const cmdlinet cmdline;
  null_message_handlert messages;
  auto check_history = [&](const irep_idt &input_name, std::size_t width)
  {
    symbolt input{input_name, unsignedbv_typet{width}, ID_Verilog};
    input.is_input = true;
    input.module = system.main_symbol->name;
    REQUIRE_FALSE(system.symbol_table.add(input));

    ebmc_propertiest properties;
    auto &p = properties.properties.emplace_back();
    p.identifier = input_name;
    auto input_expr = input.symbol_expr();
    auto previous = verilog_past_exprt{input_expr, integer_typet{}.one_expr()};
    p.normalized_expr = sva_always_exprt{equal_exprt{input_expr, previous}};

    auto netlist = make_netlist(system, properties, cmdline, messages);
    const auto &history = to_symbol_expr(
      to_equal_expr(to_sva_always_expr(p.normalized_expr).op()).rhs());
    REQUIRE(netlist.var_map.map.at(history.identifier()).bits.size() == width);
    return history.identifier();
  };

  const auto narrow = check_history("small", 8);
  const auto wide = check_history("wide", 16);
  const auto other = check_history("other", 8);
  REQUIRE(narrow != wide);
  REQUIRE(narrow != other);
  REQUIRE(wide != other);
}
