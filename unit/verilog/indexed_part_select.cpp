/*******************************************************************\

Module: Indexed Part Select Unit Tests

Author: Daniel Kroening, Amazon, dkr@amazon.com

\*******************************************************************/

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/mathematical_types.h>
#include <util/namespace.h>
#include <util/replace_expr.h>
#include <util/simplify_expr.h>
#include <util/symbol_table.h>

#include <testing-utils/use_catch.h>
#include <verilog/aval_bval_encoding.h>
#include <verilog/verilog_expr.h>
#include <verilog/verilog_types.h>

/// helper: make a 32-bit unsignedbv symbol with offset 0
static symbol_exprt src32(const irep_idt &name)
{
  auto type = unsignedbv_typet{32};
  type.set(ID_C_offset, "0");
  return symbol_exprt{name, std::move(type)};
}

SCENARIO("indexed part-select lower, constant index")
{
  // src[15 +: 8] — selects bits 15..22
  GIVEN("a +: part select with constant index")
  {
    auto src = src32("a");
    auto index = from_integer(15, integer_typet{});
    auto width = from_integer(8, integer_typet{});
    verilog_indexed_part_select_plus_or_minus_exprt ps(
      ID_verilog_indexed_part_select_plus,
      src,
      index,
      width,
      unsignedbv_typet{8});

    auto result = ps.lower();

    THEN("result is extractbits with bottom = 15")
    {
      REQUIRE(result.id() == ID_extractbits);
      auto &eb = to_extractbits_expr(result);
      REQUIRE(eb.src() == src);
      auto bottom = numeric_cast_v<mp_integer>(to_constant_expr(eb.index()));
      REQUIRE(bottom == 15);
    }
  }

  // src[15 -: 8] — selects bits 15..8, bottom = 8
  GIVEN("a -: part select with constant index")
  {
    auto src = src32("a");
    auto index = from_integer(15, integer_typet{});
    auto width = from_integer(8, integer_typet{});
    verilog_indexed_part_select_plus_or_minus_exprt ps(
      ID_verilog_indexed_part_select_minus,
      src,
      index,
      width,
      unsignedbv_typet{8});

    auto result = ps.lower();

    THEN("result is extractbits with bottom = 8")
    {
      REQUIRE(result.id() == ID_extractbits);
      auto &eb = to_extractbits_expr(result);
      REQUIRE(eb.src() == src);
      auto bottom = numeric_cast_v<mp_integer>(to_constant_expr(eb.index()));
      REQUIRE(bottom == 8);
    }
  }
}

/// Substitute into the already lowered expression to exercise the dynamic path.
static constant_exprt
select_at(exprt lowered, const symbol_exprt &index, const mp_integer &base)
{
  replace_expr(index, from_integer(base, index.type()), lowered);
  const symbol_tablet symbols;
  auto result = simplify_expr(std::move(lowered), namespacet{symbols});
  REQUIRE(result.is_constant());
  return to_constant_expr(result);
}

SCENARIO("indexed part-select lower, non-constant index")
{
  const symbol_exprt index{"i", integer_typet{}};
  const auto src = src32("a");
  for(const auto &op :
      {ID_verilog_indexed_part_select_plus,
       ID_verilog_indexed_part_select_minus})
  {
    const bool plus = op == ID_verilog_indexed_part_select_plus;
    const auto select = verilog_indexed_part_select_plus_or_minus_exprt{
      op,
      src,
      index,
      from_integer(8, integer_typet{}),
      unsignedbv_typet{8}}.lower();

    // Test each source bit at every valid base in the original 32-bit case.
    for(unsigned bit = 0; bit < 32; ++bit)
    {
      auto lowered = select;
      const auto value = uint32_t{1} << bit;
      replace_expr(src, from_integer(value, src.type()), lowered);
      for(int bottom = 0; bottom <= 24; ++bottom)
      {
        CAPTURE(plus, bit, bottom);
        const auto result =
          select_at(lowered, index, plus ? bottom : bottom + 7);
        REQUIRE(
          numeric_cast_v<mp_integer>(result) == ((value >> bottom) & 255));
      }
    }
  }
}

SCENARIO(
  "indexed part-select preserves overlapping bits and fills each missing bit")
{
  const symbol_exprt index{"i", signedbv_typet{80}};
  const auto two = unsignedbv_typet{4};
  const auto four = verilog_unsignedbv_typet{4};
  const auto width = from_integer(3, integer_typet{});
  const auto result2 = unsignedbv_typet{3};
  const auto result4 = verilog_unsignedbv_typet{3};

  for(const auto &op :
      {ID_verilog_indexed_part_select_plus,
       ID_verilog_indexed_part_select_minus})
  {
    const bool plus = op == ID_verilog_indexed_part_select_plus;
    // These indices have the same low bits as a valid base.
    const mp_integer far = power(2, 70);
    const std::vector<mp_integer> bottoms{
      -far + 1, -3, -2, -1, 0, 1, 2, 3, 4, far + 1};
    for(unsigned value = 0; value < 16; ++value)
    {
      const auto select =
        verilog_indexed_part_select_plus_or_minus_exprt{
          op, from_integer(value, two), index, width, result2}
          .lower();
      for(const auto &bottom : bottoms)
      {
        unsigned expected = 0;
        for(unsigned bit = 0; bit < 3; ++bit)
        {
          const mp_integer pos = bottom + bit;
          if(
            pos >= 0 && pos < 4 &&
            (value & (1u << numeric_cast_v<unsigned>(pos))))
            expected |= 1u << bit;
        }
        CAPTURE(plus, value, bottom);
        const auto result =
          select_at(select, index, plus ? bottom : bottom + 2);
        REQUIRE(numeric_cast_v<mp_integer>(result) == expected);
      }
    }

    const std::string digits = "z1x0";
    const auto select =
      verilog_indexed_part_select_plus_or_minus_exprt{
        op, constant_exprt{digits, four}, index, width, result4}
        .lower();
    for(const auto &bottom : bottoms)
    {
      std::string expected(3, 'x');
      for(unsigned bit = 0; bit < 3; ++bit)
      {
        const mp_integer pos = bottom + bit;
        if(pos >= 0 && pos < 4)
          expected[2 - bit] = digits[3 - numeric_cast_v<unsigned>(pos)];
      }
      CAPTURE(plus, bottom);
      const auto result = select_at(select, index, plus ? bottom : bottom + 2);
      REQUIRE(
        result.get_value() ==
        lower_to_aval_bval(constant_exprt{expected, result4}).get_value());
    }
  }
}
