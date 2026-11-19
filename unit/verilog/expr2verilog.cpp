/*******************************************************************\

Module: expr2verilog Unit Tests

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/mathematical_types.h>
#include <util/namespace.h>
#include <util/symbol_table.h>

#include <testing-utils/use_catch.h>
#include <verilog/expr2verilog.h>

SCENARIO("Verilog precedence, Boolean parity and population count")
{
  symbol_tablet symbol_table;
  namespacet ns{symbol_table};
  symbol_exprt a{"a", bool_typet{}}, b{"b", bool_typet{}}, c{"c", bool_typet{}};
  const if_exprt branch{a, b, c};
  REQUIRE(expr2verilog(if_exprt{branch, a, b}, ns) == "(a ? b : c) ? a : b");
  REQUIRE(
    expr2verilog(if_exprt{a, branch, branch}, ns) ==
    "a ? a ? b : c : a ? b : c");
  REQUIRE(expr2verilog(xor_exprt{a, b}, ns) == "a ^ b");
  REQUIRE(expr2verilog(xnor_exprt{a, b}, ns) == "a ~^ b");
  REQUIRE(expr2verilog(xnor_exprt{{a, b, c}}, ns) == "!(a ^ b ^ c)");
  REQUIRE(expr2verilog(popcount_exprt{a}, ns) == "$countones(a)");
}

SCENARIO("Output of extractbits expressions")
{
  symbol_tablet symbol_table;
  namespacet ns{symbol_table};

  GIVEN("an extractbits expression with constant index")
  {
    // bits 4 to 7 of a
    auto src = symbol_exprt{"a", unsignedbv_typet{32}};
    auto extractbits = extractbits_exprt{
      src, from_integer(4, integer_typet{}), unsignedbv_typet{4}};

    THEN("the part select has upper bound index + width - 1")
    {
      REQUIRE(expr2verilog(extractbits, ns) == "a[7:4]");
    }
  }

  GIVEN("an extractbits expression with non-constant index")
  {
    // bits i to i + 3 of a
    auto src = symbol_exprt{"a", unsignedbv_typet{32}};
    auto index = symbol_exprt{"i", integer_typet{}};
    auto extractbits = extractbits_exprt{src, index, unsignedbv_typet{4}};

    THEN("the part select has upper bound index + width - 1")
    {
      REQUIRE(expr2verilog(extractbits, ns) == "a[i + 3:i]");
    }
  }
}
