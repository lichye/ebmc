/*******************************************************************\

Module: Encoding SVA sequences

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "sequence.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/mathematical_types.h>

#include <ebmc/ebmc_error.h>
#include <temporal-logic/rewrite_sva_sequence.h>
#include <temporal-logic/temporal_logic.h>
#include <verilog/sva_expr.h>

#include "instantiate_word_level.h"
#include "obligations.h"

#include <algorithm>

struct sva_local_referencet
{
  const exprt &local;
  std::size_t offset;
  std::size_t width; // zero for a whole variable, which need not be a vector
};

static std::optional<sva_local_referencet> local_reference(const exprt &expr)
{
  if(expr.id() == ID_sva_local)
    return sva_local_referencet{expr, 0, 0};
  if(expr.id() != ID_extractbit && expr.id() != ID_extractbits)
    return {};

  const auto &src = expr.operands()[0];
  const auto base = local_reference(src);
  if(
    !base || !can_cast_type<bitvector_typet>(src.type()) ||
    (expr.type().id() != ID_bool &&
     !can_cast_type<bitvector_typet>(expr.type())))
    return {};
  const auto index = numeric_cast<std::size_t>(expr.operands()[1]);
  const auto src_width = to_bitvector_type(src.type()).get_width();
  const auto width = expr.type().id() == ID_bool
                       ? 1
                       : to_bitvector_type(expr.type()).get_width();
  if(!index || width > src_width || *index > src_width - width)
    return {};
  return sva_local_referencet{base->local, base->offset + *index, width};
}

static mp_integer slice_mask(std::size_t offset, std::size_t width)
{
  return (power(2, width) - 1) * power(2, offset);
}

std::optional<exprt> read_sva_local(const exprt &expr)
{
  const auto ref = local_reference(expr);
  if(!ref)
    return {};

  const auto &sample = static_cast<const exprt &>(ref->local.find(ID_value));
  const auto &holes = ref->local.get_string(ID_unassigned);
  if(
    sample.is_nil() ||
    (!holes.empty() &&
     (ref->width == 0 ||
      bitwise_and(
        string2integer(holes, 16), slice_mask(ref->offset, ref->width)) != 0)))
    throw ebmc_errort{}.with_location(ref->local.source_location())
      << "assertion local variable " << ref->local.get(ID_base_name)
      << " is read before assignment";

  if(ref->width == 0)
    return sample;
  if(expr.id() == ID_extractbit)
    return extractbit_exprt{sample, ref->offset};
  return extractbits_exprt{sample, ref->offset, expr.type()};
}

exprt bind_sva_locals(exprt expr, const sva_local_valuest &values)
{
  if(!values.empty())
    expr.visit_pre(
      [&](exprt &node)
      {
        if(node.id() == ID_sva_local)
        {
          auto it = values.find(node.get(ID_identifier));
          if(it != values.end())
          {
            node.set(ID_value, it->second.find(ID_value));
            if(it->second.get(ID_unassigned).empty())
              node.remove(ID_unassigned);
            else
              node.set(ID_unassigned, it->second.get(ID_unassigned));
          }
        }
      });
  return expr;
}

// Keep unwritten bits in a separate mask. Their placeholder value is never
// observable because read_sva_local checks the mask on every read.
static void
write_local_slice(exprt &local, const sva_local_referencet &ref, exprt value)
{
  const auto width = to_bitvector_type(local.type()).get_width();
  const auto end = ref.offset + ref.width;
  auto old = static_cast<const exprt &>(local.find(ID_value));
  mp_integer holes = 0;
  if(old.is_nil())
  {
    holes = slice_mask(0, width);
    old = from_integer(0, local.type());
  }
  else if(!local.get(ID_unassigned).empty())
    holes = string2integer(local.get_string(ID_unassigned), 16);

  exprt::operandst parts;
  if(end < width)
    parts.push_back(extractbits_exprt{old, end, unsignedbv_typet{width - end}});
  parts.push_back(std::move(value));
  if(ref.offset != 0)
    parts.push_back(extractbits_exprt{old, 0, unsignedbv_typet{ref.offset}});

  if(parts.size() == 1)
    local.set(
      ID_value, typecast_exprt::conditional_cast(parts.front(), local.type()));
  else
    local.set(ID_value, concatenation_exprt{std::move(parts), local.type()});

  if(holes != 0)
    holes -= bitwise_and(holes, slice_mask(ref.offset, ref.width));
  if(holes == 0)
    local.remove(ID_unassigned);
  else
    local.set(ID_unassigned, integer2string(holes, 16));
}

static void capture_locals(
  const exprt &items,
  const mp_integer &t,
  const mp_integer &no_timeframes,
  sva_local_valuest &values)
{
  // Match items are blocking assignments, in source order (1800-2017 16.10).
  for(const auto &item : items.operands())
  {
    const auto &assignment = to_binary_expr(item);
    const auto ref = local_reference(assignment.lhs()).value();
    auto value =
      assignment.rhs().is_nil()
        ? nil_exprt{}
        : instantiate(
            bind_sva_locals(assignment.rhs(), values), t, no_timeframes);
    // The target may already carry a sample from an earlier match endpoint.
    auto &local =
      values.try_emplace(ref.local.get(ID_identifier), ref.local).first->second;
    if(ref.width != 0)
      write_local_slice(local, ref, std::move(value));
    else
    {
      local.set(ID_value, std::move(value));
      local.remove(ID_unassigned);
    }
  }
}

// Parallel branches may export disjoint local variables (16.10.2).
static sva_local_valuest
parallel_locals(const sequence_matcht &lhs, const sequence_matcht &rhs)
{
  auto values = lhs.locals;
  for(const auto &entry : rhs.locals)
    if(!values.insert(entry).second)
      throw ebmc_errort{} << "assertion local variable assigned in both "
                             "parallel sequence branches";
  return values;
}

// true iff there is a pending match in the given set
static bool has_pending(const sequence_matchest &matches)
{
  for(auto &m : matches)
    if(m.is_pending())
      return true;
  return false;
}

// condition on counters for ocurrences of non-consecutive repetitions
exprt sequence_count_condition(
  const sva_sequence_repetition_exprt &expr,
  const exprt &counter)
{
  if(expr.is_range())
  {
    // number of repetitions inside a range
    auto from = numeric_cast_v<mp_integer>(expr.from());

    if(expr.is_unbounded())
    {
      return binary_relation_exprt{
        counter, ID_ge, from_integer(from, counter.type())};
    }
    else
    {
      auto to = numeric_cast_v<mp_integer>(expr.to());

      return and_exprt{
        binary_relation_exprt{
          counter, ID_ge, from_integer(from, counter.type())},
        binary_relation_exprt{
          counter, ID_le, from_integer(to, counter.type())}};
    }
  }
  else
  {
    // fixed number of repetitions
    auto repetitions = numeric_cast_v<mp_integer>(expr.repetitions());
    return equal_exprt{counter, from_integer(repetitions, counter.type())};
  }
}

/// determine type for the repetition counter
typet sequence_count_type(
  const sva_sequence_repetition_exprt &expr,
  const mp_integer &no_timeframes)
{
  mp_integer max;

  if(expr.is_range())
  {
    if(expr.is_unbounded())
      max = numeric_cast_v<mp_integer>(expr.from());
    else
      max = numeric_cast_v<mp_integer>(expr.to());
  }
  else
    max = numeric_cast_v<mp_integer>(expr.repetitions());

  max = std::max(no_timeframes, max);

  auto bits = address_bits(max + 1);
  return unsignedbv_typet{bits};
}

sequence_matchest instantiate_sequence_rec(
  exprt expr,
  const mp_integer &t,
  const mp_integer &no_timeframes)
{
  PRECONDITION(t < no_timeframes);

  if(expr.id() == ID_sva_sequence_match_items)
  {
    const auto &items = to_binary_expr(expr);
    for(const auto &item : items.rhs().operands())
    {
      const auto &target = to_binary_expr(item).lhs();
      if(!local_reference(target))
        throw ebmc_errort{}.with_location(item.find_source_location())
          << "assignment to an assertion local variable requires a fixed, "
             "in-range bit or part select";
    }
    if(expr.get_bool(ID_initial))
    {
      sva_local_valuest initial;
      capture_locals(items.rhs(), t, no_timeframes, initial);
      auto matches = instantiate_sequence_rec(
        bind_sva_locals(items.lhs(), initial), t, no_timeframes);
      // Declaration locals have the lifetime of this sequence invocation.
      for(auto &match : matches)
        for(const auto &local : initial)
          match.locals.erase(local.first);
      return matches;
    }

    auto matches = instantiate_sequence_rec(items.lhs(), t, no_timeframes);
    for(auto &match : matches)
      if(!match.is_pending() && !match.empty_match())
        capture_locals(
          items.rhs(), match.end_time(), no_timeframes, match.locals);
    matches.erase(
      std::remove_if(
        matches.begin(),
        matches.end(),
        [](const sequence_matcht &m) { return m.empty_match(); }),
      matches.end());
    return matches;
  }
  if(expr.id() == ID_sva_cycle_delay) // ##[1:2] something
  {
    auto &sva_cycle_delay_expr = to_sva_cycle_delay_expr(expr);

    // This is the product of the match points on the LHS and RHS,
    // with appropriate delay in between.
    sequence_matchest lhs_matches;

    if(sva_cycle_delay_expr.lhs().is_nil())
    {
      // defaults to a length-1 match with condition 'true'
      lhs_matches = {{t, true_exprt{}}};
    }
    else
    {
      lhs_matches =
        instantiate_sequence_rec(sva_cycle_delay_expr.lhs(), t, no_timeframes);
    }

    sequence_matchest result;

    for(auto &lhs_match : lhs_matches)
    {
      if(lhs_match.empty_match())
        continue; // handled by rewrite_sva_sequence

      // A pending match represents a sequence that may complete beyond
      // the bound. Propagate without evaluating the RHS.
      if(lhs_match.is_pending())
      {
        result.push_back(lhs_match);
        continue;
      }

      // now delay
      const auto from = numeric_cast_v<mp_integer>(sva_cycle_delay_expr.from());
      DATA_INVARIANT(from >= 0, "##n must not be negative");

      auto t_rhs_from = lhs_match.end_time() + from;
      mp_integer t_rhs_to;

      if(!sva_cycle_delay_expr.is_range()) // ##1 something
      {
        t_rhs_to = t_rhs_from;
      }
      else if(sva_cycle_delay_expr.is_unbounded()) // ##[from:$] something
      {
        // one beyond the bound
        t_rhs_to = no_timeframes;
      }
      else // ##[from:to] something
      {
        auto to = numeric_cast_v<mp_integer>(sva_cycle_delay_expr.to());
        DATA_INVARIANT(to >= from, "##[a:b] must have a<=b");
        t_rhs_to = lhs_match.end_time() + to;
      }

      // Add a potential match for each timeframe in the range
      for(mp_integer t_rhs = t_rhs_from; t_rhs <= t_rhs_to; ++t_rhs)
      {
        // Do we exceed the bound?
        if(t_rhs >= no_timeframes)
        {
          result.push_back(
            sequence_matcht::pending_match(lhs_match.condition()));
        }
        else // still inside bound
        {
          const auto rhs_matches = instantiate_sequence_rec(
            bind_sva_locals(sva_cycle_delay_expr.rhs(), lhs_match.locals),
            t_rhs,
            no_timeframes);

          for(auto &rhs_match : rhs_matches)
          {
            // empty matches are handled by rewrite_sva_sequence
            if(!rhs_match.empty_match())
            {
              auto cond =
                and_exprt{lhs_match.condition(), rhs_match.condition()};
              if(rhs_match.is_pending())
              {
                result.push_back(
                  sequence_matcht::pending_match(std::move(cond)));
              }
              else
              {
                auto values = rhs_match.locals;
                // A later assignment supersedes an earlier one.
                values.insert(lhs_match.locals.begin(), lhs_match.locals.end());
                result.emplace_back(
                  rhs_match.end_time(), std::move(cond), std::move(values));
              }
            }
          }
        }
      }
    }

    return result;
  }
  else if(expr.id() == ID_sva_cycle_delay_star) // ##[*] something
  {
    auto &cycle_delay_star = to_sva_cycle_delay_star_expr(expr);
    return instantiate_sequence_rec(cycle_delay_star.lower(), t, no_timeframes);
  }
  else if(expr.id() == ID_sva_cycle_delay_plus) // ##[+] something
  {
    auto &cycle_delay_plus = to_sva_cycle_delay_plus_expr(expr);
    return instantiate_sequence_rec(cycle_delay_plus.lower(), t, no_timeframes);
  }
  else if(expr.id() == ID_sva_sequence_intersect)
  {
    // IEEE 1800-2017 16.9.6
    // For the intersection of the two operand sequences to match, the following
    // must hold:
    // — Both the operands shall match.
    // — The lengths of the two matches of the operand sequences shall be the same.
    auto &intersect = to_sva_sequence_intersect_expr(expr);

    const auto lhs_matches =
      instantiate_sequence_rec(intersect.lhs(), t, no_timeframes);
    const auto rhs_matches =
      instantiate_sequence_rec(intersect.rhs(), t, no_timeframes);

    sequence_matchest result;

    exprt::operandst concrete_match_conditions;

    for(auto &lhs_match : lhs_matches)
    {
      for(auto &rhs_match : rhs_matches)
      {
        if(!lhs_match.is_pending() && !rhs_match.is_pending())
        {
          // Same length?
          if(lhs_match.end_time() == rhs_match.end_time())
          {
            auto cond = and_exprt{lhs_match.condition(), rhs_match.condition()};
            concrete_match_conditions.push_back(cond);
            result.emplace_back(
              lhs_match.end_time(),
              std::move(cond),
              parallel_locals(lhs_match, rhs_match));
          }
        }
      }
    }

    // If either operand has a pending match, the intersect might
    // still match beyond the bound. The pending condition is:
    // no concrete match has fired.
    if(has_pending(lhs_matches) || has_pending(rhs_matches))
    {
      auto no_concrete = concrete_match_conditions.empty()
                           ? static_cast<exprt>(true_exprt{})
                           : not_exprt{disjunction(concrete_match_conditions)};
      result.push_back(sequence_matcht::pending_match(no_concrete));
    }

    return result;
  }
  else if(expr.id() == ID_sva_sequence_first_match)
  {
    auto &first_match = to_sva_sequence_first_match_expr(expr);

    const auto matches =
      instantiate_sequence_rec(first_match.sequence(), t, no_timeframes);

    // first_match(seq): the match of seq with the earliest ending
    // clock tick. In the symbolic setting, we must encode that a
    // match at time t fires only if no earlier match has fired.
    sequence_matchest result;

    // Collect concrete matches sorted by end_time (they already are).
    // Build an "no earlier match" guard for each match point.
    exprt no_earlier_match = true_exprt{};

    // Group by end_time and process in order.
    std::map<mp_integer, std::vector<const sequence_matcht *>> by_time;
    for(auto &match : matches)
    {
      if(!match.is_pending())
        by_time[match.end_time()].push_back(&match);
    }

    for(auto &[end_time, at_time] : by_time)
    {
      exprt::operandst conditions;
      for(const auto *match : at_time)
        conditions.push_back(match->condition());
      auto any_match_here = disjunction(conditions);
      if(std::all_of(
           at_time.begin(),
           at_time.end(),
           [](const sequence_matcht *match) { return match->locals.empty(); }))
        result.emplace_back(
          end_time, and_exprt{no_earlier_match, any_match_here});
      else
        for(const auto *match : at_time)
          result.emplace_back(
            end_time,
            and_exprt{no_earlier_match, match->condition()},
            match->locals);
      // For the next time point, add the condition that no match
      // fired at this time point or earlier.
      no_earlier_match = and_exprt{no_earlier_match, not_exprt{any_match_here}};
    }

    // Propagate pending matches, guarded by no concrete match firing.
    for(auto &match : matches)
    {
      if(match.is_pending())
      {
        result.push_back(sequence_matcht::pending_match(
          and_exprt{no_earlier_match, match.condition()}));
      }
    }

    return result;
  }
  else if(expr.id() == ID_sva_sequence_throughout)
  {
    // IEEE 1800-2017 16.9.9
    // exp throughout seq matches along an interval provided
    // - seq matches along the interval and
    // - exp evaluates to true at each clock tick of the interval.
    auto &throughout = to_sva_sequence_throughout_expr(expr);

    const auto matches =
      instantiate_sequence_rec(throughout.sequence(), t, no_timeframes);

    sequence_matchest result;

    for(auto &match : matches)
    {
      if(match.is_pending())
      {
        // Propagate pending status.
        result.push_back(match);
        continue;
      }

      exprt::operandst conjuncts = {match.condition()};

      for(mp_integer new_t = t; new_t <= match.end_time(); ++new_t)
      {
        auto lhs_inst =
          instantiate_state_predicate(throughout.lhs(), new_t, no_timeframes);
        conjuncts.push_back(lhs_inst);
      }

      result.emplace_back(
        match.end_time(), conjunction(conjuncts), match.locals);
    }

    return result;
  }
  else if(expr.id() == ID_sva_sequence_within)
  {
    // If the lhs match is contained anywhere within the rhs match,
    // then return the rhs match.

    auto &within_expr = to_sva_sequence_within_expr(expr);
    const auto matches_rhs =
      instantiate_sequence_rec(within_expr.rhs(), t, no_timeframes);

    sequence_matchest result;
    bool has_pending = false;
    exprt::operandst concrete_match_conditions;

    for(auto &match_rhs : matches_rhs)
    {
      if(match_rhs.is_pending())
      {
        has_pending = true;
        continue;
      }

      for(auto start_lhs = t; start_lhs <= match_rhs.end_time(); ++start_lhs)
      {
        auto matches_lhs =
          instantiate_sequence_rec(within_expr.lhs(), start_lhs, no_timeframes);

        for(auto &match_lhs : matches_lhs)
        {
          if(match_lhs.is_pending())
          {
            // The RHS has already ended inside the verification window.
            // An LHS that can only finish beyond that window cannot fit
            // inside this concrete RHS match.
            continue;
          }

          // The end_time of the lhs match must be no later than the
          // end_time of the rhs match.
          if(match_lhs.end_time() <= match_rhs.end_time())
          {
            // return the rhs end_time
            auto cond = and_exprt{match_lhs.condition(), match_rhs.condition()};
            concrete_match_conditions.push_back(cond);
            // Only the containing sequence exports its locals (16.10.2).
            result.emplace_back(
              match_rhs.end_time(), std::move(cond), match_rhs.locals);
          }
        }
      }
    }

    // Only a pending containing interval can extend 'within' beyond the
    // bound. A pending inner match cannot extend a completed RHS interval.
    if(has_pending)
    {
      auto no_concrete = concrete_match_conditions.empty()
                           ? static_cast<exprt>(true_exprt{})
                           : not_exprt{disjunction(concrete_match_conditions)};
      result.push_back(sequence_matcht::pending_match(no_concrete));
    }

    return result;
  }
  else if(expr.id() == ID_sva_and)
  {
    // IEEE 1800-2017 16.9.5
    // 1. Both operands must match.
    // 2. Both sequences start at the same time.
    // 3. The end time of the composite sequence is
    //    the end time of the operand sequence that completes last.
    auto &and_expr = to_sva_and_expr(expr);
    auto matches_lhs =
      instantiate_sequence_rec(and_expr.lhs(), t, no_timeframes);
    auto matches_rhs =
      instantiate_sequence_rec(and_expr.rhs(), t, no_timeframes);

    sequence_matchest result;
    exprt::operandst concrete_match_conditions;

    for(auto &match_lhs : matches_lhs)
      for(auto &match_rhs : matches_rhs)
      {
        if(!match_lhs.is_pending() && !match_rhs.is_pending())
        {
          auto end_time = std::max(match_lhs.end_time(), match_rhs.end_time());
          auto cond = and_exprt{match_lhs.condition(), match_rhs.condition()};
          concrete_match_conditions.push_back(cond);
          result.emplace_back(
            std::move(end_time),
            std::move(cond),
            parallel_locals(match_lhs, match_rhs));
        }
      }

    // If either operand has a pending match, the 'and' might
    // still match beyond the bound. The pending condition is:
    // no concrete match has fired.
    if(has_pending(matches_lhs) || has_pending(matches_rhs))
    {
      auto no_concrete = concrete_match_conditions.empty()
                           ? static_cast<exprt>(true_exprt{})
                           : not_exprt{disjunction(concrete_match_conditions)};
      result.push_back(sequence_matcht::pending_match(no_concrete));
    }

    return result;
  }
  else if(expr.id() == ID_sva_or)
  {
    // IEEE 1800-2017 16.9.7
    // The set of matches of a or b is the set union of the matches of a
    // and the matches of b.
    sequence_matchest result;

    for(auto &op : expr.operands())
      for(auto &match : instantiate_sequence_rec(op, t, no_timeframes))
      {
        result.push_back(match);
      }

    return result;
  }
  else if(expr.id() == ID_sva_sequence_repetition_star) // [*...]
  {
    auto &repetition = to_sva_sequence_repetition_star_expr(expr);
    if(repetition.is_empty_match())
    {
      // [*0] denotes the empty match.
      return {sequence_matcht::empty_match(t)};
    }
    else if(repetition.is_unbounded() && repetition.repetitions_given())
    {
      // op[*from:$] -> op[*from:to]
      // with to = no_timeframes - t
      auto to = from_integer(no_timeframes - t, integer_typet{});
      auto new_repetition = sva_sequence_repetition_star_exprt{
        repetition.op(), repetition.from(), to};

      return instantiate_sequence_rec(new_repetition.lower(), t, no_timeframes);
    }
    else
    {
      // [*], [*n], [*x:y]
      return instantiate_sequence_rec(repetition.lower(), t, no_timeframes);
    }
  }
  else if(expr.id() == ID_sva_sequence_repetition_plus) // [+]
  {
    auto &repetition = to_sva_sequence_repetition_plus_expr(expr);
    return instantiate_sequence_rec(repetition.lower(), t, no_timeframes);
  }
  else if(
    expr.id() == ID_sva_sequence_goto_repetition ||          // [->...]
    expr.id() == ID_sva_sequence_non_consecutive_repetition) // [=...]
  {
    auto &repetition = to_sva_sequence_repetition_expr(expr);
    auto &condition = repetition.op();

    sequence_matchest result;

    // We add up the number of matches of 'op' starting from
    // timeframe u, until the end of our unwinding.
    const auto type = sequence_count_type(repetition, no_timeframes);
    const auto zero = from_integer(0, type);
    const auto one = from_integer(1, type);
    exprt matches = zero;

    for(mp_integer u = t; u < no_timeframes; ++u)
    {
      // match of op in timeframe u?
      auto rec_op = instantiate(condition, u, no_timeframes);

      // add up
      matches = plus_exprt{matches, if_exprt{rec_op, one, zero}};

      // We have a match for op[->n] if there is a match in timeframe
      // u and matches is n.
      // We have a match for op[=n] if matches is n.
      auto count_cond = sequence_count_condition(repetition, matches);
      if(expr.id() == ID_sva_sequence_goto_repetition)
        result.emplace_back(u, and_exprt{rec_op, count_cond});
      else
        result.emplace_back(u, count_cond);
    }

    // Weak semantics: the sequence could still complete beyond the
    // bound. Add a pending match when the count hasn't yet reached
    // the required minimum.
    auto min = repetition.is_range()
                 ? numeric_cast_v<mp_integer>(repetition.from())
                 : numeric_cast_v<mp_integer>(repetition.repetitions());
    result.push_back(sequence_matcht::pending_match(binary_relation_exprt{
      matches, ID_lt, from_integer(min, matches.type())}));

    return result;
  }
  else if(expr.id() == ID_sva_boolean)
  {
    // a state predicate
    auto &predicate = to_sva_boolean_expr(expr).op();
    auto instantiated =
      instantiate_state_predicate(predicate, t, no_timeframes);
    return {{t, instantiated}};
  }
  else
  {
    DATA_INVARIANT_WITH_DIAGNOSTICS(
      false, "unexpected sequence expression", expr.pretty());
  }
}

sequence_matchest instantiate_sequence(
  exprt expr,
  const mp_integer &t,
  const mp_integer &no_timeframes)
{
  auto rewritten = rewrite_sva_sequence(expr);
  return instantiate_sequence_rec(rewritten, t, no_timeframes);
}
