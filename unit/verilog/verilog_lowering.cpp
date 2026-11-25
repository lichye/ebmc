/*******************************************************************\

Module: Verilog Lowering Unit Tests

Author: Daniel Kroening, Amazon, dkr@amazon.com

\*******************************************************************/

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/mathematical_types.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>

#include <testing-utils/use_catch.h>
#include <verilog/aval_bval_encoding.h>
#include <verilog/verilog_lowering.h>
#include <verilog/verilog_types.h>

/// helper: a constant of the given Verilog real type, with the value
/// encoded as for the back-ends
static constant_exprt real_constant(double value, const typet &type)
{
  ieee_float_valuet f{ieee_float_spect::double_precision()};
  if(type.id() == ID_verilog_shortreal)
    f = ieee_float_valuet{ieee_float_spect::single_precision()};
  f.from_double(value);
  return constant_exprt{f.to_expr().get_value(), type};
}

static const exprt round_to_even =
  ieee_floatt::rounding_mode_expr(ieee_floatt::rounding_modet::ROUND_TO_EVEN);

SCENARIO("verilog_lowering projects an extract within a concatenation operand")
{
  const symbol_exprt high{"high", signedbv_typet{3}};
  const symbol_exprt mid{"mid", bool_typet{}};
  const symbol_exprt low{"low", unsignedbv_typet{4}};
  const concatenation_exprt packed{{high, mid, low}, unsignedbv_typet{8}};
  const auto index = [](int i) { return from_integer(i, integer_typet{}); };

  REQUIRE(
    verilog_lowering(extractbits_exprt{packed, index(5), high.type()}) == high);
  REQUIRE(verilog_lowering(extractbit_exprt{packed, index(4)}) == mid);
  REQUIRE(
    verilog_lowering(extractbit_exprt{packed, index(2)}) ==
    extractbit_exprt{low, index(2)});
  const signedbv_typet signed_slice{2};
  REQUIRE(
    verilog_lowering(extractbits_exprt{packed, index(6), signed_slice}) ==
    extractbits_exprt{high, index(1), signed_slice});

  const extractbits_exprt spanning{packed, index(3), unsignedbv_typet{2}};
  const extractbits_exprt outside{packed, index(7), unsignedbv_typet{2}};
  const extractbit_exprt dynamic{packed, symbol_exprt{"i", unsignedbv_typet{3}}};
  REQUIRE(verilog_lowering(spanning) == spanning);
  REQUIRE(verilog_lowering(outside) == outside);
  REQUIRE(verilog_lowering(dynamic) == dynamic);

  const auto encoded_type = lower_to_aval_bval(verilog_unsignedbv_typet{2});
  const concatenation_exprt encoded{
    {symbol_exprt{"xz", encoded_type}, low}, unsignedbv_typet{8}};
  const extractbit_exprt encoded_extract{encoded, index(6)};
  REQUIRE(verilog_lowering(encoded_extract) == encoded_extract);
}

SCENARIO("verilog_lowering lowers real arithmetic to IEEE 754 operations")
{
  auto real = verilog_real_typet{};
  auto floatbv = verilog_lowering(real);
  auto a = real_constant(1.5, real);
  auto b = real_constant(2.0, real);
  auto c = real_constant(4.0, real);

  GIVEN("binary arithmetic on reals")
  {
    const std::pair<exprt, irep_idt> cases[] = {
      {plus_exprt{a, b}, ID_floatbv_plus},
      {minus_exprt{a, b}, ID_floatbv_minus},
      {mult_exprt{a, b}, ID_floatbv_mult},
      {div_exprt{a, b}, ID_floatbv_div}};

    for(auto &[expr, id] : cases)
    {
      THEN("the result is " + id2string(id) + " with round-to-even")
      {
        auto result = verilog_lowering(expr);
        REQUIRE(result.id() == id);
        REQUIRE(result.type() == floatbv);
        auto &op = to_ieee_float_op_expr(result);
        REQUIRE(op.lhs() == verilog_lowering(a));
        REQUIRE(op.rhs() == verilog_lowering(b));
        REQUIRE(op.rounding_mode() == round_to_even);
      }
    }
  }

  GIVEN("a sum with three operands")
  {
    plus_exprt expr{a, b};
    expr.add_to_operands(c);

    THEN("the operands are added left to right")
    {
      auto result = verilog_lowering(expr);
      REQUIRE(result.id() == ID_floatbv_plus);
      auto &outer = to_ieee_float_op_expr(result);
      REQUIRE(outer.rhs() == verilog_lowering(c));
      REQUIRE(outer.lhs().id() == ID_floatbv_plus);
      auto &inner = to_ieee_float_op_expr(outer.lhs());
      REQUIRE(inner.lhs() == verilog_lowering(a));
      REQUIRE(inner.rhs() == verilog_lowering(b));
    }
  }

  GIVEN("shortreal arithmetic")
  {
    auto shortreal = verilog_shortreal_typet{};
    plus_exprt expr{
      real_constant(1.5, shortreal), real_constant(2.0, shortreal)};

    THEN("the result is single precision")
    {
      auto result = verilog_lowering(expr);
      REQUIRE(result.id() == ID_floatbv_plus);
      REQUIRE(result.type() == verilog_lowering(shortreal));
      REQUIRE(to_floatbv_type(result.type()).get_width() == 32);
    }
  }
}

SCENARIO("verilog_lowering lowers the type of a conditional on reals")
{
  auto real = verilog_real_typet{};
  auto a = real_constant(1.5, real);
  auto b = real_constant(2.5, real);
  symbol_exprt cond{"c", bool_typet{}};

  GIVEN("a conditional with real operands")
  {
    if_exprt expr{cond, a, b, real};

    THEN("the conditional and its operands have the floatbv type")
    {
      auto result = verilog_lowering(expr);
      REQUIRE(result.id() == ID_if);
      REQUIRE(result.type() == verilog_lowering(real));
      REQUIRE(to_if_expr(result).true_case() == verilog_lowering(a));
      REQUIRE(to_if_expr(result).false_case() == verilog_lowering(b));
    }
  }
}

SCENARIO("verilog_lower_symbol_types lowers the Verilog-specific types")
{
  symbol_tablet symbol_table;

  auto add = [&symbol_table](const irep_idt &name, const typet &type)
  {
    symbolt symbol{name, type, ID_Verilog};
    symbol.is_state_var = true;
    symbol.is_lvalue = true;
    symbol_table.insert(std::move(symbol));
  };

  add("r", verilog_real_typet{});
  add("s", verilog_shortreal_typet{});
  add("t", verilog_realtime_typet{});
  add("h", verilog_chandle_typet{});
  add("e", verilog_event_typet{});
  // an unsignedbv is a lowered type already, and is left unchanged
  add("b", unsignedbv_typet{8});

  // a parameter: substituted by its value, and left unchanged
  {
    symbolt p{"p", verilog_real_typet{}, ID_Verilog};
    p.is_macro = true;
    symbol_table.insert(std::move(p));
  }

  verilog_lower_symbol_types(symbol_table);

  THEN("the real types become floatbv, keeping the Verilog type")
  {
    for(const auto &name : {"r", "t"})
    {
      auto &type = symbol_table.lookup_ref(name).type;
      REQUIRE(type == ieee_float_spect::double_precision().to_type());
    }

    auto &s_type = symbol_table.lookup_ref("s").type;
    REQUIRE(s_type == ieee_float_spect::single_precision().to_type());
    REQUIRE(s_type.get(ID_C_verilog_type) == ID_verilog_shortreal);
  }

  THEN("chandle and event become bv, keeping the Verilog type")
  {
    auto &h_type = symbol_table.lookup_ref("h").type;
    REQUIRE(h_type.id() == ID_bv);
    REQUIRE(h_type.get(ID_C_verilog_type) == ID_verilog_chandle);

    auto &e_type = symbol_table.lookup_ref("e").type;
    REQUIRE(e_type.id() == ID_bv);
    REQUIRE(e_type.get(ID_C_verilog_type) == ID_verilog_event);
  }

  THEN("already-lowered and parameter symbols are unchanged")
  {
    REQUIRE(symbol_table.lookup_ref("b").type == unsignedbv_typet{8});
    REQUIRE(symbol_table.lookup_ref("p").type == verilog_real_typet{});
  }
}

SCENARIO(
  "Lowering preserves shared aggregate operands and their annotations",
  "[core][verilog][verilog_lowering]")
{
  const unsignedbv_typet word{8};
  const struct_typet packet{{{"data", word}}};
  struct_exprt payload{{symbol_exprt{"data", word}}, packet};
  payload.add_source_location().set_file("shared.sv");
  const if_exprt shared{symbol_exprt{"select", bool_typet{}}, payload, payload};

  const auto unchanged = verilog_lowering(shared);
  REQUIRE(&unchanged.read() == &shared.read());
  REQUIRE(&to_if_expr(unchanged).true_case().read() == &payload.read());
  REQUIRE(
    to_if_expr(unchanged).false_case().source_location().get_file() ==
    "shared.sv");

  const auto real = verilog_real_typet{};
  const struct_typet mixed{{{"packet", packet}, {"fraction", real}}};
  const struct_exprt source{{shared, real_constant(1.5, real)}, mixed};
  const auto lowered = verilog_lowering(source);
  REQUIRE(&lowered.read() != &source.read());
  REQUIRE(&lowered.operands().front().read() == &shared.read());
  REQUIRE(lowered.operands().back().type() == verilog_lowering(real));
}
