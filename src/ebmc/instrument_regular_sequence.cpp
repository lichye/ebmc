#include "instrument_regular_sequence.h"

#include <optional>

#include <util/arith_tools.h>
#include <util/expr_util.h>

#include <temporal-logic/ctl.h>
#include <temporal-logic/temporal_logic.h>
#include <trans-word-level/next_symbol.h>
#include <verilog/sva_expr.h>

#include "ebmc_error.h"

namespace
{
constexpr std::size_t max_positions = 1024;

struct positiont
{
  std::optional<exprt> forward;
  std::optional<exprt> repeat;
  std::optional<std::size_t> skip;
};

class sequencet
{
public:
  std::vector<positiont> positions{1};
  bool nonempty = false;
  bool regular_repeat = false;

  bool append(const exprt &sequence)
  {
    if(sequence.id() == ID_sva_boolean)
    {
      const auto &test = to_sva_boolean_expr(sequence).op();
      return boolean(test) && segment(test, 1, 1);
    }

    if(sequence.id() == ID_sva_sequence_repetition_star)
    {
      const auto &repeat = to_sva_sequence_repetition_star_expr(sequence);
      if(repeat.op().id() != ID_sva_boolean)
        return false;

      const auto &test = to_sva_boolean_expr(repeat.op()).op();
      if(!boolean(test))
        return false;
      if(repeat.is_singleton())
      {
        auto count = numeric_cast<std::size_t>(repeat.repetitions());
        return count && segment(test, *count, *count);
      }
      auto lower = repeat.is_range()
                     ? numeric_cast<std::size_t>(repeat.from())
                     : std::optional<std::size_t>{0};
      if(!lower)
        return false;

      if(!repeat.is_range() || repeat.is_unbounded())
      {
        regular_repeat = true;
        return segment(test, *lower, {});
      }

      auto upper = numeric_cast<std::size_t>(repeat.to());
      regular_repeat |= *lower == 0;
      return upper && segment(test, *lower, *upper);
    }

    if(sequence.id() != ID_sva_cycle_delay)
      return false;

    const auto &delay = to_sva_cycle_delay_expr(sequence);
    auto lower = numeric_cast<std::size_t>(delay.from());
    if(!lower || delay.is_unbounded())
      return false;
    auto upper = delay.is_range() ? numeric_cast<std::size_t>(delay.to()) : lower;
    if(!upper)
      return false;

    const bool leading_delay = delay.lhs().is_nil();
    if(!leading_delay && (*lower == 0 || *upper == 0 || !append(delay.lhs())))
      return false; // ##0 fusion uses a shared sample; leave it to Spot.

    const std::size_t adjustment = leading_delay ? 0 : 1;
    return segment(true_exprt(), *lower - adjustment, *upper - adjustment) &&
           append(delay.rhs());
  }

  static bool boolean(const exprt &expr)
  {
    return expr.type().id() == ID_bool && !has_temporal_operator(expr);
  }

private:
  bool segment(
    const exprt &test,
    std::size_t minimum,
    std::optional<std::size_t> maximum)
  {
    const auto length = maximum.value_or(minimum);
    if(length < minimum || length >= max_positions)
      return false;
    const auto extra = length + (maximum ? 0 : 1);
    if(extra > max_positions - positions.size())
      return false;

    nonempty |= minimum != 0;
    const auto first = positions.size() - 1;
    const auto last = first + length;
    positions.resize(last + 1);
    for(auto state = first; state < last; ++state)
    {
      auto &position = positions[state];
      position.forward = test;
      if(state - first >= minimum)
        position.skip = last;
    }

    if(!maximum)
    {
      // Another segment may leave this state on the same sample on which
      // a repeat can continue. Preserve both possibilities.
      positions.back().repeat = test;
      positions.back().skip = last + 1;
      positions.emplace_back();
    }
    return true;
  }

};

symbol_exprt make_state(
  transition_systemt &system,
  const symbolt &property,
  const std::string &suffix)
{
  auxiliary_symbolt state{
    id2string(property.name) + "::sequence_" + suffix,
    bool_typet{},
    property.mode};
  state.module = property.module;
  state.is_state_var = true;
  const auto signal = state.symbol_expr();
  if(system.symbol_table.add(std::move(state)))
    throw ebmc_errort{} << "duplicate sequence monitor state " << signal.identifier();
  return signal;
}
} // namespace

bool instrument_regular_sequence(
  transition_systemt &system,
  ebmc_propertiest::propertyt &property,
  const symbolt &property_symbol)
{
  const auto &formula = property.normalized_expr;
  if(formula.id() != ID_G && formula.id() != ID_sva_always)
    return false;
  const auto &abort = to_unary_expr(formula).op();
  if(abort.id() != ID_sva_accept_on)
    return false;
  const auto &reset = to_binary_expr(abort).lhs();
  const auto *rule = &to_binary_expr(abort).rhs();
  exprt::operandst immediate;
  if(rule->id() == ID_and)
  {
    const auto &conjuncts = rule->operands();
    rule = nullptr;
    for(const auto &conjunct : conjuncts)
    {
      if(sequencet::boolean(conjunct))
        immediate.push_back(conjunct);
      else if(rule)
        return false;
      else
        rule = &conjunct;
    }
    if(!rule)
      return false;
  }

  const bool nonoverlapped = rule->id() == ID_sva_non_overlapped_implication;
  if(!nonoverlapped && rule->id() != ID_sva_overlapped_implication)
    return false;
  const auto &implication = to_sva_implication_base_expr(*rule);
  const auto &consequent = implication.property();
  if(!sequencet::boolean(reset) || !sequencet::boolean(consequent))
    return false;

  sequencet sequence;
  if(
    !sequence.append(implication.sequence()) || !sequence.nonempty ||
    !sequence.regular_repeat)
    return false;

  exprt::operandst initial, transition;
  const auto count = sequence.positions.size();
  std::vector<exprt::operandst> destinations(count), active(count);
  std::vector<symbol_exprt> state;
  state.reserve(count);
  for(std::size_t i = 0; i < count; ++i)
  {
    state.push_back(make_state(system, property_symbol, std::to_string(i)));
    active[i].push_back(state.back());
    initial.push_back(not_exprt{state.back()});
  }

  // At each sample start one more attempt. All other active positions are
  // exactly the prefixes that consumed through the previous sample.
  active.front().push_back(true_exprt());
  const auto enabled = not_exprt{reset};
  for(std::size_t i = 0; i < count; ++i)
  {
    const auto &position = sequence.positions[i];
    const auto available = disjunction(active[i]);
    if(position.skip)
      active[*position.skip].push_back(available);
    if(position.forward)
      destinations[i + 1].push_back(
        conjunction({enabled, available, *position.forward}));
    if(position.repeat)
      destinations[i].push_back(
        conjunction({enabled, available, *position.repeat}));
  }

  // Acceptance may also skip trailing optional positions, but must consume
  // a real sample. Thus a stale state or an empty launch cannot match.
  std::vector<bool> accepts(count, false);
  accepts.back() = true;
  exprt::operandst matches;
  for(std::size_t i = count; i-- > 0;)
  {
    const auto skip = sequence.positions[i].skip;
    if(skip)
      accepts[i] = accepts[*skip];
    const auto arrived = disjunction(destinations[i]);
    if(accepts[i])
      matches.push_back(arrived);
    transition.push_back(equal_exprt{next_symbol_exprt{state[i]}, arrived});
  }
  auto matched = disjunction(matches);

  if(nonoverlapped)
  {
    auto pending = make_state(system, property_symbol, "pending");
    initial.push_back(not_exprt{pending});
    transition.push_back(equal_exprt{next_symbol_exprt{pending}, matched});
    matched = std::move(pending);
  }

  system.trans_expr.init() =
    and_exprt{system.trans_expr.init(), conjunction(initial)};
  system.trans_expr.trans() =
    and_exprt{system.trans_expr.trans(), conjunction(transition)};
  // An instantaneous conjunct is due on its own sample. Reset can only
  // discharge it on that sample. The sequence consequent is due at each
  // match endpoint (or the following sample for a nonoverlapped rule).
  immediate.push_back(implies_exprt{matched, consequent});
  property.normalized_expr =
    AG_exprt{implies_exprt{enabled, conjunction(immediate)}};
  return true;
}
