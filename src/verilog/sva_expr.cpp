/*******************************************************************\

Module: System Verilog Asssertion Expressions

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "sva_expr.h"

#include <util/arith_tools.h>
#include <util/mathematical_types.h>

exprt sva_iff_exprt::implications() const
{
  return sva_and_exprt{
    sva_implies_exprt{lhs(), rhs()}, sva_implies_exprt{rhs(), lhs()}, type()};
}

exprt sva_cycle_delay_plus_exprt::lower() const
{
  // same as ##[1:$]
  return sva_cycle_delay_exprt{
    lhs(),
    from_integer(1, integer_typet{}),
    exprt{ID_infinity, integer_typet{}},
    rhs()};
}

exprt sva_cycle_delay_star_exprt::lower() const
{
  // same as ##[0:$]
  return sva_cycle_delay_exprt{
    lhs(),
    from_integer(0, integer_typet{}),
    exprt{ID_infinity, integer_typet{}},
    rhs()};
}

exprt sva_case_exprt::lower() const
{
  auto &case_items = this->case_items();

  exprt result = true_exprt{};
  for(const auto &item : case_items)
    if(item.patterns().empty())
      result = item.result();

  // The first matching item takes priority, regardless of the default position.
  for(auto it = case_items.rbegin(); it != case_items.rend(); ++it)
  {
    if(it->patterns().empty())
      continue;
    exprt::operandst disjuncts;
    for(const auto &pattern : it->patterns())
      disjuncts.push_back(equal_exprt{
        typecast_exprt::conditional_cast(case_op(), pattern.type()), pattern});
    result = if_exprt{disjunction(disjuncts), it->result(), std::move(result)};
  }
  return result;
}

exprt sva_sequence_repetition_plus_exprt::lower() const
{
  // [+] is the same as [*1:$]
  return sva_sequence_repetition_star_exprt{
    op(), from_integer(1, integer_typet{}), infinity_exprt{integer_typet{}}};
}

exprt sva_sequence_repetition_star_exprt::lower() const
{
  PRECONDITION(
    op().type().id() == ID_bool || op().type().id() == ID_verilog_sva_sequence);

  if(!repetitions_given())
  {
    // op[*] is the same as op[*0:$]
    return sva_sequence_repetition_star_exprt{
      op(), from_integer(0, integer_typet{}), infinity_exprt{integer_typet{}}};
  }
  else if(is_empty_match())
  {
    // [*0] is a special case, denoting the empty match
    PRECONDITION(false);
  }
  else if(!is_range())
  {
    // expand x[*n] into x ##1 x ##1 ...
    auto n = numeric_cast_v<mp_integer>(repetitions());
    PRECONDITION(n >= 1);

    exprt result = op();

    for(; n >= 2; --n)
    {
      result = sva_cycle_delay_exprt{
        std::move(result), from_integer(1, integer_typet{}), nil_exprt{}, op()};
    }

    return result;
  }
  else if(is_unbounded())
  {
    PRECONDITION(false);
  }
  else // bounded range
  {
    // expand x[*a:b] into x[*a] or x[*a+1] or ... or x[*b]
    auto from_int = numeric_cast_v<mp_integer>(from());
    auto to_int = numeric_cast_v<mp_integer>(to());

    DATA_INVARIANT(from_int >= 0, "number of repetitions must not be negative");
    DATA_INVARIANT(
      to_int >= from_int, "number of repetitions must be interval");

    exprt result = sva_sequence_repetition_star_exprt{op(), from()};

    for(mp_integer n = from_int + 1; n <= to_int; ++n)
    {
      auto n_expr = from_integer(n, integer_typet{});
      result = sva_or_exprt{
        std::move(result),
        sva_sequence_repetition_star_exprt{op(), n_expr},
        verilog_sva_sequence_typet{}};
    }

    return result;
  }
}
