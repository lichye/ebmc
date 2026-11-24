#include "witness_lasso.h"

#include <util/arith_tools.h>
#include <util/expr_util.h>
#include <util/simplify_expr.h>

#include <temporal-logic/nnf.h>
#include <trans-word-level/cone_of_influence.h>
#include <trans-word-level/instantiate_word_level.h>
#include <trans-word-level/lasso.h>
#include <trans-word-level/property.h>
#include <trans-word-level/trans_trace_word_level.h>
#include <trans-word-level/unwind.h>
#include <verilog/sva_expr.h>
#include <verilog/verilog_bits.h>
#include <verilog/verilog_expr.h>
#include <verilog/verilog_initializer.h>
#include <verilog/verilog_rtl.h>

#include "ebmc_error.h"
#include "ebmc_solver_factory.h"
#include "instrument_buechi.h"
#include "instrument_past.h"
#include "property_checker.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <new>
#include <set>
#include <stdexcept>
#include <unordered_set>

#ifndef _WIN32
#  include <sys/resource.h>
#endif

namespace
{
constexpr std::uint64_t mib = 1024 * 1024;

std::optional<std::uint64_t> witness_memory_budget(const cmdlinet &cmdline)
{
  if(!cmdline.isset("witness-lasso-memory"))
    return {};

  const auto &arg = cmdline.get_value("witness-lasso-memory");
  std::uint64_t size;
  const auto parsed =
    std::from_chars(arg.data(), arg.data() + arg.size(), size);
  if(
    parsed.ec != std::errc{} || parsed.ptr != arg.data() + arg.size() ||
    !size || size > std::numeric_limits<std::uint64_t>::max() / mib)
    throw ebmc_errort{}
      << "--witness-lasso-memory requires a positive size in MiB";
  return size * mib;
}

#ifdef __linux__
// An SMT solver is a child of EBMC and inherits its address-space limit.
// Leave room for both processes and for cgroup charges outside their heaps.
std::uint64_t cgroup_witness_memory_budget(std::uint64_t limit)
{
  std::ifstream cgroups("/proc/self/cgroup");
  std::string line;
  while(std::getline(cgroups, line))
  {
    if(line.compare(0, 4, "0::/") != 0)
      continue;
    auto path = line.substr(3);
    while(!path.empty())
    {
      std::uint64_t bytes;
      if(std::ifstream("/sys/fs/cgroup" + path + "/memory.max") >> bytes)
      {
        const auto reserve = std::min<std::uint64_t>(bytes / 4, 256 * mib);
        limit = std::min(limit, (bytes - reserve) / 2);
      }
      path.resize(path.rfind('/'));
    }
    break;
  }
  return limit;
}

bool process_below_limit(std::uint64_t bytes)
{
  std::ifstream status("/proc/self/status");
  std::string line;
  while(std::getline(status, line))
  {
    if(line.compare(0, 7, "VmSize:") == 0)
    {
      const auto first = line.find_first_not_of(" \t", 7);
      std::uint64_t kib;
      if(first == std::string::npos)
        break;
      const auto parsed =
        std::from_chars(line.data() + first, line.data() + line.size(), kib);
      if(parsed.ec == std::errc{})
        return kib < bytes / 1024;
      break;
    }
  }
  return false;
}
#endif

class lasso_memory_limitt
{
public:
  explicit lasso_memory_limitt(std::uint64_t limit)
  {
#if defined(RLIMIT_AS) && !defined(_WIN32)
    if(getrlimit(RLIMIT_AS, &previous) != 0)
      return;
#  ifdef __linux__
    limit = cgroup_witness_memory_budget(limit);
#  endif
    if(previous.rlim_cur != RLIM_INFINITY)
      limit = std::min<std::uint64_t>(limit, previous.rlim_cur);
#  ifdef __linux__
    if(!process_below_limit(limit))
      return;
#  endif
    if(limit >= std::numeric_limits<rlim_t>::max())
      return;
    const rlimit bounded{static_cast<rlim_t>(limit), previous.rlim_max};
    active = setrlimit(RLIMIT_AS, &bounded) == 0;
#else
    (void)limit;
#endif
  }

  ~lasso_memory_limitt()
  {
#if defined(RLIMIT_AS) && !defined(_WIN32)
    if(active)
      setrlimit(RLIMIT_AS, &previous);
#endif
  }

  bool available() const
  {
    return active;
  }

private:
  bool active = false;
#if defined(RLIMIT_AS) && !defined(_WIN32)
  // RLIMIT_AS is process-wide. Serialize scoped changes by linked callers.
  inline static std::mutex limiter_mutex;
  const std::unique_lock<std::mutex> lock{limiter_mutex};
  rlimit previous{};
#endif
};

bool invariant(const exprt &expr)
{
  return (expr.id() == ID_AG || expr.id() == ID_G ||
          expr.id() == ID_sva_always) &&
         !has_temporal_operator(to_unary_expr(expr).op());
}

// In native BMC a false obligation in this fragment is final on the finite
// prefix. Assertions include pending sequence matches, so a failed positive
// sequence has no possible pending match. Covers exclude pending matches, so
// falsifying the negation of a sequence requires an actual completed match.
bool definitive_prefix(const exprt &nnf, bool allow_pending)
{
  if(!has_temporal_operator(nnf))
    return true;
  const auto id = nnf.id();
  if(id == ID_AG || id == ID_G || id == ID_sva_always)
    return definitive_prefix(to_unary_expr(nnf).op(), allow_pending);
  if(
    id == ID_sva_overlapped_implication ||
    id == ID_sva_non_overlapped_implication)
    return definitive_prefix(to_binary_expr(nnf).rhs(), allow_pending);
  if(
    id == ID_X || id == ID_AX || id == ID_sva_s_nexttime ||
    id == ID_sva_nexttime)
    return definitive_prefix(to_unary_expr(nnf).op(), allow_pending);
  if(
    id == ID_sva_ranged_always || id == ID_sva_s_always ||
    id == ID_sva_eventually || id == ID_sva_ranged_s_eventually)
    return definitive_prefix(nnf.operands().back(), allow_pending);
  if(
    id == ID_sva_accept_on || id == ID_sva_sync_accept_on ||
    id == ID_sva_reject_on || id == ID_sva_sync_reject_on)
    return definitive_prefix(to_sva_abort_expr(nnf).property(), allow_pending);
  if(
    id == ID_sva_overlapped_followed_by ||
    id == ID_sva_nonoverlapped_followed_by)
  {
    const auto &lhs = to_binary_expr(nnf).lhs();
    // With a single-cycle antecedent all its candidate matches are known.
    // A constant zero repetition has only an empty match.
    const bool empty =
      lhs.id() == ID_sva_sequence_repetition_star &&
      lhs.operands().size() == 3 && lhs.operands()[1].is_zero() &&
      (lhs.operands()[2].is_nil() || lhs.operands()[2].is_zero());
    return (!has_temporal_operator(lhs) ||
            (lhs.id() == ID_sva_boolean &&
             !has_temporal_operator(to_unary_expr(lhs).op())) ||
            empty) &&
           definitive_prefix(to_binary_expr(nnf).rhs(), allow_pending);
  }
  if(id == ID_if && !has_temporal_operator(to_if_expr(nnf).cond()))
    return definitive_prefix(to_if_expr(nnf).true_case(), allow_pending) &&
           definitive_prefix(to_if_expr(nnf).false_case(), allow_pending);
  if(id == ID_or || id == ID_and)
    return std::all_of(
      nnf.operands().begin(),
      nnf.operands().end(),
      [&](const exprt &op) { return definitive_prefix(op, allow_pending); });
  const bool negated = id == ID_not;
  const auto seq = (negated ? to_unary_expr(nnf).op() : nnf).id();
  return (negated || allow_pending) &&
         (seq == ID_sva_implicit_strong || seq == ID_sva_implicit_weak ||
          seq == ID_sva_strong || seq == ID_sva_weak);
}

// A definition may drive a whole current-frame wire or a constant, in-range
// projection of it. For overlap and dependency analysis we need only a
// consistent coordinate system, not the Verilog spelling of the indices.
struct wire_locationt
{
  irep_idt name;
  std::optional<verilog_rtl_slicet> slice;

  bool overlaps(const wire_locationt &other) const
  {
    return name == other.name &&
           (!slice || !other.slice || slice->overlaps(*other.slice));
  }
};

std::optional<mp_integer> packed_width(const typet &type)
{
  if(can_cast_type<bitvector_typet>(type))
    return to_bitvector_type(type).get_width();
  return verilog_bits_opt(type);
}

bool ground_index(const exprt &expr)
{
  if(expr.is_constant())
    return true;
  if(
    expr.id() != ID_plus && expr.id() != ID_minus &&
    expr.id() != ID_unary_minus && expr.id() != ID_typecast)
    return false;
  return std::all_of(
    expr.operands().begin(), expr.operands().end(), ground_index);
}

std::optional<mp_integer> constant_index(const exprt &expr)
{
  if(expr.is_constant())
    return numeric_cast<mp_integer>(expr);
  if(ground_index(expr))
    return numeric_cast<mp_integer>(simplify_expr(expr, multi_namespacet{}));
  return {};
}

const exprt *selected_constructor_operand(const exprt &expr)
{
  if(expr.id() == ID_member)
  {
    const auto &member = to_member_expr(expr);
    const auto &compound = member.struct_op();
    if(compound.id() == ID_struct && compound.type().id() == ID_struct)
    {
      const auto &type = to_struct_type(compound.type());
      const auto component = type.component_number(member.get_component_name());
      if(component < compound.operands().size())
        return &compound.operands()[component];
    }
  }
  else if(expr.id() == ID_index)
  {
    const auto &index = to_index_expr(expr);
    if(index.array().id() == ID_array)
    {
      const auto part = constant_index(index.index());
      if(part && *part >= 0 && *part < index.array().operands().size())
        return &index.array().operands()[part->to_ulong()];
    }
  }
  return nullptr;
}

std::optional<wire_locationt> wire_location(const exprt &expr)
{
  if(expr.id() == ID_symbol)
    return wire_locationt{to_symbol_expr(expr).get_identifier(), {}};
  if(const auto *operand = selected_constructor_operand(expr))
    return wire_location(*operand);

  const exprt *base;
  std::optional<mp_integer> offset, width;
  if(expr.id() == ID_index)
  {
    const auto &index = to_index_expr(expr);
    base = &index.array();
    if(base->type().id() != ID_array)
      return {};
    const auto size =
      numeric_cast<mp_integer>(to_array_type(base->type()).size());
    const auto part = constant_index(index.index());
    width = packed_width(expr.type());
    if(!size || !part || *part < 0 || *part >= *size || !width)
      return {};
    offset = *part * *width;
  }
  else if(expr.id() == ID_extractbit || expr.id() == ID_extractbits)
  {
    base = &expr.operands().front();
    if(!can_cast_type<bitvector_typet>(base->type()))
      return {};
    offset = constant_index(expr.operands()[1]);
    width = packed_width(expr.type());
    if(
      !offset || !width || *offset < 0 ||
      *offset + *width > to_bitvector_type(base->type()).get_width())
      return {};
  }
  else
    return {};

  if(*width <= 0)
    return {};
  auto location = wire_location(*base);
  if(location)
  {
    if(location->slice)
      *offset += location->slice->lower;
    location->slice.emplace(*offset, *offset + *width - 1);
  }
  return location;
}

// These lowered operations give a value for every valuation of their inputs.
// Array select/store, aggregate construction and bit-vector division,
// including the backend's result for division by zero, impose no extra
// restriction on the inputs. An unknown expression or next() fails closed.
bool total_operation(const exprt &expr)
{
  const auto type = expr.type().id(), id = expr.id();
  if(expr.is_constant())
    return true;
  if(
    type != ID_bool && type != ID_signedbv && type != ID_unsignedbv &&
    type != ID_bv && type != ID_floatbv && type != ID_array &&
    type != ID_struct)
    return false;
  if(id == ID_div || id == ID_mod)
    return type == ID_signedbv || type == ID_unsignedbv;
  return id == ID_symbol || id == ID_nondet_symbol || id == ID_not ||
         id == ID_and || id == ID_or || id == ID_xor || id == ID_implies ||
         id == ID_equal || id == ID_notequal || id == ID_floatbv_plus ||
         id == ID_bitnot || id == ID_bitand || id == ID_bitor ||
         id == ID_bitxor || id == ID_plus || id == ID_minus || id == ID_mult ||
         id == ID_unary_minus || id == ID_extractbits || id == ID_extractbit ||
         id == ID_shl || id == ID_lshr || id == ID_ashr ||
         id == ID_replication || id == ID_typecast || id == ID_if ||
         id == ID_lt || id == ID_le || id == ID_gt || id == ID_ge ||
         id == ID_concatenation || id == ID_array || id == ID_struct ||
         id == ID_index || id == ID_member || id == ID_with ||
         id == ID_zero_extend || id == ID_reduction_and ||
         id == ID_reduction_or || id == ID_reduction_xor ||
         (id == ID_constraint_select_one && !expr.operands().empty());
}

struct wire_definitiont
{
  wire_locationt target;
  const exprt *value;
};

using wire_definition_mapt = std::map<irep_idt, std::vector<wire_definitiont>>;

bool acyclic_wire_definitions(const wire_definition_mapt &wires)
{
  std::set<const wire_definitiont *> active, done;
  std::function<bool(const wire_definitiont &)> acyclic =
    [&](const wire_definitiont &wire)
  {
    if(done.count(&wire))
      return true;
    if(!active.insert(&wire).second)
      return false;

    std::vector<const exprt *> pending{wire.value};
    std::unordered_set<const irept::dt *> visited;
    while(!pending.empty())
    {
      const auto &expr = *pending.back();
      pending.pop_back();
      if(!visited.insert(&expr.read()).second)
        continue;
      if(expr.id() == ID_verilog_past)
      {
        // A positive delay reads stored history. A zero delay still reads
        // the current frame and must participate in cycle detection.
        const auto &past = to_verilog_past_expr(expr);
        if(numeric_cast<mp_integer>(past.ticks()) == mp_integer{0})
          pending.push_back(&past.what());
        continue;
      }
      // An in-range constant projection only depends on its own slice.
      // A dynamic read visits the whole aggregate and the index expression.
      if(const auto *operand = selected_constructor_operand(expr))
        pending.push_back(operand);
      else if(auto read = wire_location(expr))
      {
        auto defs = wires.find(read->name);
        if(defs != wires.end())
          for(const auto &definition : defs->second)
            if(read->overlaps(definition.target) && !acyclic(definition))
              return false;
      }
      else
        for(const auto &op : expr.operands())
          pending.push_back(&op);
    }

    active.erase(&wire);
    done.insert(&wire);
    return true;
  };

  for(const auto &entry : wires)
    for(const auto &definition : entry.second)
      if(!acyclic(definition))
        return false;
  return true;
}

class total_valuet
{
public:
  bool operator()(const exprt &root)
  {
    if(root.id() == ID_symbol)
      return true;
    std::vector<const exprt *> pending{&root};
    std::unordered_set<const irept::dt *> visited;
    while(!pending.empty())
    {
      const auto &expr = *pending.back();
      pending.pop_back();
      const auto *node = &expr.read();
      if(checked.count(node) || !visited.insert(node).second)
        continue;
      if(expr.id() == ID_verilog_past)
      {
        const auto &past = to_verilog_past_expr(expr);
        const auto ticks = numeric_cast<mp_integer>(past.ticks());
        if(
          !ticks || *ticks < 0 ||
          (*ticks > 0 && !verilog_default_initializer(past.type())))
          return false;
        // The implicit history registers start with the same default as word
        // unwinding. After evaluating the current frame they sample what()
        // and shift older values. Check the sampled expression even though a
        // positive delay introduces no current-frame dependency at its use.
        pending.push_back(&past.what());
        continue;
      }
      if(!total_operation(expr))
        return false;
      for(const auto &op : expr.operands())
        pending.push_back(&op);
    }
    checked.merge(visited);
    return true;
  }

private:
  // Cache only successfully checked subexpressions. A failed RHS stays in the
  // reduced relation and must never become eligible for removal on reuse.
  std::unordered_set<const irept::dt *> checked;
};

// Sufficient syntactic totality criterion. A freely chosen next value is
// allowed for an unassigned latch; written latches are distinct and each RHS
// is a total function of the current frame and sampled history. Initial
// conditions do not constrain successors. State constraints that are not
// discharged by a forced successor value must pass the definition check.
bool total_transition(
  const transition_systemt &system,
  const transt &tr,
  total_valuet &total_value)
{
  // A complete monitor can be composed independently of the design and the
  // other monitors. Only use its construction proof while both exact factors
  // remain present. No other constraint may write its private next state.
  std::map<exprt, irep_idt> complete;
  for(const auto &monitor : system.complete_monitors)
  {
    std::vector<const exprt *> conjuncts{&tr.init()};
    while(!conjuncts.empty())
    {
      const auto &c = *conjuncts.back();
      conjuncts.pop_back();
      if(c == monitor.initial)
      {
        complete.emplace(monitor.transition, monitor.state);
        break;
      }
      if(c.id() == ID_and)
        for(const auto &op : c.operands())
          conjuncts.push_back(&op);
    }
  }
  std::set<irep_idt> assigned;
  const auto all_conjuncts =
    [&](const exprt &root, const auto &check, bool transition = false)
  {
    std::vector<const exprt *> pending{&root};
    while(!pending.empty())
    {
      auto &expr = *pending.back();
      pending.pop_back();
      if(transition && complete.count(expr))
      {
        if(!assigned.insert(complete.at(expr)).second)
          return false;
      }
      else if(expr.id() == ID_and)
        for(const auto &op : expr.operands())
          pending.push_back(&op);
      else if(!expr.is_true() && !check(expr))
        return false;
    }
    return true;
  };
  std::set<irep_idt> successor_true;
  if(!all_conjuncts(
       tr.trans(),
       [&](const exprt &clause)
       {
         if(clause.id() != ID_equal)
           return false;
         const auto &eq = to_equal_expr(clause);
         if(
           eq.lhs().id() != ID_next_symbol ||
           eq.lhs().type() != eq.rhs().type())
           return false;
         const auto id = eq.lhs().get(ID_identifier);
         const auto *sym = system.symbol_table.lookup(id);
         const bool stutter =
           eq.rhs().id() == ID_symbol && eq.rhs().get(ID_identifier) == id;
         if(
           !sym || !sym->is_state_var || !assigned.insert(id).second ||
           (!stutter && !total_value(eq.rhs())))
           return false;
         if(eq.rhs().is_true())
           successor_true.insert(id);
         return true;
       },
       true))
    return false;

  const auto writable_wire =
    [&](const exprt &lhs) -> std::optional<wire_locationt>
  {
    const auto location = wire_location(lhs);
    if(location)
    {
      const auto *symbol = system.symbol_table.lookup(location->name);
      if(symbol && !symbol->is_state_var)
        return location;
    }
    return {};
  };

  wire_definition_mapt wires;
  const auto occupied = [&](const wire_locationt &location)
  {
    auto it = wires.find(location.name);
    return it != wires.end() && std::any_of(
                                  it->second.begin(),
                                  it->second.end(),
                                  [&](const wire_definitiont &d)
                                  { return location.overlaps(d.target); });
  };
  const auto define = [&](const wire_locationt &target, const exprt &rhs)
  {
    if(occupied(target))
      return false;
    wires[target.name].push_back({target, &rhs});
    return true;
  };

  struct aliast
  {
    wire_locationt lhs, rhs;
    const equal_exprt *equation;
  };
  std::vector<aliast> aliases;
  if(!all_conjuncts(
       tr.invar(),
       [&](const exprt &clause)
       {
         // A BTOR2 import can retain initial conditions as constraints
         // guarded by a latch that is always true after the first step.
         // The prefix still checks the original invariant in every frame.
         if(
           clause.id() == ID_or &&
           std::any_of(
             clause.operands().begin(),
             clause.operands().end(),
             [&](const exprt &op)
             {
               return op.id() == ID_symbol && op.is_boolean() &&
                      successor_true.count(to_symbol_expr(op).identifier());
             }))
           return true;
         if(clause.id() != ID_equal)
           return false;
         const auto &eq = to_equal_expr(clause);
         if(eq.lhs().type() != eq.rhs().type())
           return false;
         auto lhs = writable_wire(eq.lhs());
         auto rhs = writable_wire(eq.rhs());
         if(!total_value(eq.lhs()) || !total_value(eq.rhs()))
           return false;
         if(eq.lhs() == eq.rhs())
           return true;
         if(lhs && rhs)
         {
           aliases.push_back({std::move(*lhs), std::move(*rhs), &eq});
           return true;
         }
         return lhs ? define(*lhs, eq.rhs()) : rhs && define(*rhs, eq.lhs());
       }))
    return false;

  // Orient an alias away from an already defined end. When neither end is
  // defined, defer it until all aliases connected to existing definitions
  // have been processed, then choose a free root for the remaining component.
  while(!aliases.empty())
  {
    const auto before = aliases.size();
    auto first_deferred = std::remove_if(
      aliases.begin(),
      aliases.end(),
      [&](const aliast &alias)
      {
        const bool lhs = occupied(alias.lhs), rhs = occupied(alias.rhs);
        if(lhs == rhs)
          return false;
        const auto &eq = *alias.equation;
        return lhs ? define(alias.rhs, eq.lhs()) : define(alias.lhs, eq.rhs());
      });
    aliases.erase(first_deferred, aliases.end());
    if(aliases.size() == before)
    {
      const auto &alias = aliases.back();
      if(occupied(alias.rhs) || !define(alias.lhs, alias.equation->rhs()))
        return false;
      aliases.pop_back();
    }
  }
  return acyclic_wire_definitions(wires);
}

// Match values on both sides of the repeated frame. This includes the
// assertion and assumption monitors, inputs appearing under next(), and
// implicit per-frame choices which need not have a symbol-table entry.
void collect_boundary(const exprt &expr, std::set<exprt> &boundary)
{
  expr.visit_pre(
    [&](const exprt &sub)
    {
      if(sub.id() == ID_symbol || sub.id() == ID_next_symbol)
        boundary.emplace(symbol_exprt{sub.get(ID_identifier), sub.type()});
      else if(sub.id() == ID_nondet_symbol)
        boundary.insert(sub);
    });
}

// A deterministic monitor assigns next(state) with a priority mux over its
// current state. SMT define-fun expansion of large muxes across time frames
// is costly in CVC5. Equalities to distinct integer state numbers are mutually
// exclusive, so their implications plus the default case are equivalent.
void split_state_muxes(exprt &trans)
{
  if(trans.id() == ID_and)
  {
    for(auto &op : trans.operands())
      split_state_muxes(op);
    return;
  }
  if(trans.id() != ID_equal)
    return;

  const auto &assignment = to_equal_expr(trans);
  const auto &next = assignment.lhs();
  if(next.id() != ID_next_symbol || next.type().id() != ID_range)
    return;

  const auto state = symbol_exprt{next.get(ID_identifier), next.type()};
  const exprt *rhs = &assignment.rhs();
  exprt::operandst guards, constraints;
  std::set<mp_integer> seen;
  while(rhs->id() == ID_if)
  {
    const auto &mux = to_if_expr(*rhs);
    const auto &guard = mux.cond();
    if(guard.id() != ID_equal)
      break;
    const auto &condition = to_equal_expr(guard);
    if(condition.lhs() != state || condition.rhs().type() != state.type())
      break;
    auto value = numeric_cast<mp_integer>(condition.rhs());
    if(
      !value || !to_integer_range_type(state.type()).includes(*value) ||
      !seen.insert(*value).second)
      break;

    guards.push_back(guard);
    constraints.push_back(
      implies_exprt{guard, equal_exprt{next, mux.true_case()}});
    rhs = &mux.false_case();
  }
  if(guards.size() > 1)
  {
    constraints.push_back(
      implies_exprt{not_exprt{disjunction(guards)}, equal_exprt{next, *rhs}});
    trans = conjunction(constraints);
  }
}

void certify(
  const transition_systemt &system,
  const transt &trans,
  const namespacet &trace_ns,
  const std::vector<const ebmc_propertiest::propertyt *> &assumptions,
  property_checker_resultt::propertyt &property,
  std::size_t end,
  std::optional<std::size_t> bmc_bound,
  const ebmc_solver_factoryt &factory,
  message_handlert &messages,
  const witness_lasso_observert &observe_lasso)
{
  messaget message{messages};
  message.status() << property.name << ": certifying witness loop within "
                   << end << " transitions" << messaget::eom;
  const namespacet ns{system.symbol_table};
  auto backend = factory(ns, messages);
  auto &solver = backend.decision_procedure();
  // Use the original finite obligation and its polarity. In particular its
  // pending matches must never be re-evaluated at the extension horizon.
  exprt::operandst obligations;
  bool native_loop = false;
  if(bmc_bound)
  {
    if(*bmc_bound > end || !bmc_supports_property(property.normalized_expr))
    {
      message.status()
        << property.name
        << ": original BMC horizon or property cannot be certified"
        << messaget::eom;
      property.inconclusive();
      return;
    }
    obligations = ::property(
      property.normalized_expr,
      !property.is_exists_path(),
      messages,
      *bmc_bound + 1);
    native_loop = uses_lasso_symbol(obligations);
    if(native_loop)
      end = *bmc_bound;
  }
  const auto frames = end + 1;
  unwind(trans, messages, solver, frames, ns);

  std::set<exprt> boundary;
  for(const auto &reg : system.state_variables())
    boundary.insert(reg);
  for(const auto &input : system.inputs())
    boundary.insert(input);
  collect_boundary(system.trans_expr, boundary);
  for(const auto *assumption : assumptions)
  {
    const auto &predicate = to_unary_expr(assumption->normalized_expr).op();
    collect_boundary(predicate, boundary);
    for(std::size_t t = 0; t < frames; ++t)
      solver.set_to_true(instantiate(predicate, t, frames));
  }

  // Search afresh for the same violation. The primary engine may have put
  // the bad frame at the very end of its prefix.
  collect_boundary(property.normalized_expr, boundary);
  exprt::operandst predicates;
  if(bmc_bound)
  {
    for(const auto &obligation : obligations)
      predicates.push_back(solver.handle(obligation));
  }
  else
  {
    const auto &predicate = to_unary_expr(property.normalized_expr).op();
    for(std::size_t t = 0; t < end; ++t)
      predicates.push_back(solver.handle(instantiate(predicate, t, frames)));
  }
  solver.set_to_false(conjunction(predicates));
  predicates.resize(frames, true_exprt());

  // A shorter lasso can be unrolled around its loop to end at this frame.
  // Only one loop selector per earlier frame is needed.
  exprt::operandst loops;
  for(std::size_t start = 0; start < end; ++start)
  {
    exprt::operandst equalities;
    for(const auto &variable : boundary)
      equalities.emplace_back(equal_exprt{
        instantiate(variable, start, frames),
        instantiate(variable, end, frames)});
    auto equal = conjunction(equalities);
    if(native_loop)
    {
      // Select exactly one native lasso and certify that very same back
      // edge, including all history and assumption monitor state.
      auto selected = lasso_symbol(start, end);
      solver.set_to_true(implies_exprt{selected, equal});
      loops.push_back(solver.handle(selected));
    }
    else
    {
      // A free Boolean selector has a scalar model value even when an SMT
      // solver abbreviates a large conjunction using a shared expression.
      auto selected = symbol_exprt{
        "ebmc::witness_loop::" + std::to_string(start), bool_typet{}};
      solver.set_to_true(implies_exprt{selected, equal});
      loops.push_back(selected);
    }
  }
  if(native_loop)
  {
    for(std::size_t i = 1; i < end; ++i)
      for(std::size_t k = 0; k < i; ++k)
        solver.set_to_false(lasso_symbol(k, i));
    exprt preceding = false_exprt();
    for(const auto &loop : loops)
    {
      solver.set_to_false(and_exprt{preceding, loop});
      preceding = solver.handle(or_exprt{preceding, loop});
    }
  }
  solver.set_to_true(disjunction(loops));

  switch(solver())
  {
  case decision_proceduret::resultt::D_UNSATISFIABLE:
    message.status() << property.name << ": no witness loop within " << end
                     << " transitions" << messaget::eom;
    property.inconclusive();
    return;
  case decision_proceduret::resultt::D_ERROR:
    message.status() << property.name
                     << ": witness lasso solver did not complete within the "
                        "certification resource limits"
                     << messaget::eom;
    property.inconclusive();
    return;
  case decision_proceduret::resultt::D_SATISFIABLE:
    break;
  }

  for(std::size_t start = 0; start < end; ++start)
    if(solver.get(loops[start]).is_true())
    {
      auto trace = compute_trans_trace(
        predicates, solver, frames, trace_ns, system.main_symbol->name);
      auto bad = trace.get_min_failing_timeframe();
      if(bad && (bmc_bound ? *bad <= *bmc_bound : *bad < end))
      {
        if(observe_lasso)
          observe_lasso(
            {system,
             trans,
             assumptions,
             property,
             solver,
             frames,
             start,
             *bad});
        trace.loop_start = start;
        property.witness_certificate =
          property_checker_resultt::propertyt::witness_certificatet::LASSO;
        property.witness_trace = std::move(trace);
        message.status() << property.name << ": witness loop " << start
                         << " -> " << end << " (equal endpoints), bad frame "
                         << *bad << messaget::eom;
        return;
      }
      break;
    }

  property.failure("solver did not return the witness loop or bad frame");
}

bool needs_lasso(const ebmc_propertiest::propertyt &p)
{
  return p.witness_certificate ==
           ebmc_propertiest::propertyt::witness_certificatet::NONE &&
         !p.is_assumption() &&
         (p.is_exists_path() ? p.is_proved() : p.is_refuted());
}

void certify_pending_lassos(
  const cmdlinet &solver_options,
  const transition_systemt &original_system,
  property_checker_resultt &result,
  std::size_t budget,
  std::optional<std::size_t> bmc_bound,
  message_handlert &messages,
  const witness_lasso_observert &observe_lasso)
{
  using certificate = property_checker_resultt::propertyt::witness_certificatet;
  // The candidate engine has finished. Instrument the certification model
  // privately so linked callers can reuse their model and property batch.
  auto system = original_system;
  system.main_symbol =
    &system.symbol_table.lookup_ref(original_system.main_symbol->name);
  ebmc_propertiest batch;
  batch.properties = result.properties;
  // Model past in active assumptions and goals still requiring a certificate.
  // A discharged sibling's private history is not part of the original machine.
  for(auto &p : batch.properties)
    if(
      !p.is_assumption() && !p.is_assumed() &&
      (p.witness_certificate != certificate::NONE ||
       !(p.is_exists_path() ? p.is_proved() : p.is_refuted())))
      p.normalized_expr = true_exprt();
  bool compiled = true;
  for(const auto &p : batch.properties)
    if(
      !p.is_disabled() && p.is_assumption() && p.normalized_expr.is_true() &&
      has_temporal_operator(to_unary_expr(p.original_expr).op()))
      compiled = false; // fairness erased by a liveness-to-safety transform

  try
  {
    instrument_past(system, batch);
    instrument_buechi(system, batch, true, messages, true);
  }
  catch(const ebmc_errort &error)
  {
    messaget{messages}.warning() << error.what() << messaget::eom;
    compiled = false;
  }
  std::vector<const ebmc_propertiest::propertyt *> assumptions;
  bool supported_assumptions = compiled;
  for(const auto &p : batch.properties)
    if(!p.is_disabled() && (p.is_assumption() || p.is_assumed()))
    {
      if(p.is_assumed() && invariant(p.normalized_expr))
        assumptions.push_back(&p);
      else
        supported_assumptions = false;
    }

  // Keep the primary engine's transition system and solver selection intact.
  // An engine may ignore an output flag and return a real verdict. The
  // secondary query always needs a solver, never a formula writer.
  auto trans = system.trans_expr;
  if(solver_options.isset("cvc5"))
    split_state_muxes(trans.trans());

  for(auto &p : batch.properties)
  {
    // An engine can mark an earlier finite refutation inconclusive after
    // encountering an unsupported assumption. Do not export that prefix.
    if(p.is_inconclusive())
      p.witness_trace.reset();
    if(!needs_lasso(p))
      continue;

    auto horizon = bmc_bound;
    if(
      !horizon && !invariant(p.normalized_expr) &&
      bmc_supports_property(p.normalized_expr))
    {
      // Checking an unbounded goal on an actual lasso is sound for the
      // path fragment accepted by native BMC. Branching CTL is excluded.
      horizon = p.witness_trace && !p.witness_trace->states.empty()
                  ? std::min(budget, p.witness_trace->states.size() - 1)
                  : budget;
    }
    p.witness_trace.reset();
    if(!supported_assumptions || (!horizon && !invariant(p.normalized_expr)))
    {
      messaget{messages}.status()
        << p.name << ": witness loop requires invariant monitor predicates"
        << messaget::eom;
      p.inconclusive();
      continue;
    }

    certify(
      system,
      trans,
      namespacet{original_system.symbol_table},
      assumptions,
      p,
      budget,
      horizon,
      ebmc_solver_factory(solver_options),
      messages,
      observe_lasso);
  }
  auto source = batch.properties.begin();
  for(auto &p : result.properties)
    p.copy_results_from(*source++);
}
} // namespace

std::optional<std::size_t>
witness_lasso_budget(const cmdlinet &cmdline, std::optional<std::size_t> depth)
{
  (void)witness_memory_budget(cmdline);
  if(cmdline.isset("finite-prefix"))
  {
    if(cmdline.isset("witness-lasso"))
      throw ebmc_errort{} << "--finite-prefix conflicts with --witness-lasso";
    return {};
  }
  if(depth && *depth == std::numeric_limits<std::size_t>::max())
    throw ebmc_errort{} << "BMC frame count overflows";
  if(cmdline.isset("witness-lasso"))
  {
    const auto &arg = cmdline.get_value("witness-lasso");
    std::size_t n;
    const auto p = std::from_chars(arg.data(), arg.data() + arg.size(), n);
    if(
      p.ec != std::errc{} || p.ptr != arg.data() + arg.size() || !n ||
      n == std::numeric_limits<std::size_t>::max())
      throw ebmc_errort{}
        << "--witness-lasso requires a positive transition bound";
    return n;
  }
  const auto base = depth.value_or(0);
  if(base >= std::numeric_limits<std::size_t>::max() - 48)
    throw ebmc_errort{} << "default witness budget overflows";
  return base + 48;
}

void certify_property_witnesses(
  const cmdlinet &cmdline,
  const transition_systemt &original_system,
  property_checker_resultt &result,
  std::optional<std::size_t> bmc_bound,
  message_handlert &messages)
{
  certify_property_witnesses(
    cmdline, original_system, result, bmc_bound, messages, {});
}

void certify_property_witnesses(
  const cmdlinet &cmdline,
  const transition_systemt &original_system,
  property_checker_resultt &result,
  std::optional<std::size_t> bmc_bound,
  message_handlert &messages,
  const witness_lasso_observert &observe_lasso)
{
  const auto budget = witness_lasso_budget(cmdline, bmc_bound);
  if(
    !budget ||
    result.status != property_checker_resultt::statust::VERIFICATION_RESULT)
    return;
  using certificate = property_checker_resultt::propertyt::witness_certificatet;
  const bool no_assumptions = std::none_of(
    result.properties.begin(),
    result.properties.end(),
    [](const auto &p)
    { return !p.is_disabled() && (p.is_assumption() || p.is_assumed()); });
  std::optional<bool> total_for_bdd;
  total_valuet total_value;
  auto solver_options = cmdline;
  for(const auto option : {"outfile", "show-formula", "smt2"})
    if(solver_options.isset(option))
      solver_options.set(option, false);
  bool need_witness = false;
  for(auto &p : result.properties)
  {
    p.witness_certificate = certificate::NONE;
    if(p.is_inconclusive() || p.is_unsupported() || p.is_failure())
      p.witness_trace.reset();
    if(
      !p.is_assumption() &&
      (p.is_exists_path() ? p.is_proved() : p.is_refuted()))
    {
      const bool try_totality =
        no_assumptions && !cmdline.isset("witness-lasso");
      const bool exact_bdd =
        try_totality && !bmc_bound && !cmdline.isset("coi");
      if(exact_bdd && p.proof_via == "BDD CTL" && !total_for_bdd)
        total_for_bdd = total_transition(
          original_system, original_system.trans_expr, total_value);
      if(
        exact_bdd && (p.proof_via == "BDD CTL, total relation" ||
                      (p.proof_via == "BDD CTL" && *total_for_bdd)))
      {
        p.witness_certificate = certificate::TOTAL_BDD;
        messaget{messages}.status()
          << p.name << ": exact BDD verdict on a total transition system"
          << messaget::eom;
        continue;
      }
      auto bad = p.witness_trace ? p.witness_trace->get_min_failing_timeframe()
                                 : std::optional<std::size_t>{};
      if(
        try_totality &&
        (invariant(p.normalized_expr) ||
         !has_temporal_operator(p.normalized_expr) ||
         definitive_prefix(
           property_nnf(p.normalized_expr), !p.is_exists_path())))
      {
        // Omitted definitions can be evaluated in dependency order to extend
        // the reduced trace. Keep all constraints, including definitions
        // whose RHS we cannot establish as total.
        const auto reduced = cone_of_influence(
          original_system.trans_expr,
          {p.normalized_expr},
          std::ref(total_value));
        if(!total_transition(original_system, reduced.trans, total_value))
        {
          need_witness = true;
          continue;
        }
        // A COI witness is a projection. Establish a reachable full-model
        // bad prefix before relying on totality; do the same for traceless IC3.
        if(
          cmdline.isset("coi") || !bad ||
          (!bmc_bound && !invariant(p.normalized_expr)))
        {
          const auto depth = bmc_bound.value_or(std::min(
            *budget, bad.value_or(p.witness_depth_hint.value_or(*budget))));
          const namespacet ns{original_system.symbol_table};
          auto backend = ebmc_solver_factory(solver_options)(ns, messages);
          auto &solver = backend.decision_procedure();
          unwind(original_system.trans_expr, messages, solver, depth + 1, ns);
          auto handles = ::property(
            p.normalized_expr, !p.is_exists_path(), messages, depth + 1);
          for(auto &h : handles)
            h = solver.handle(h);
          solver.set_to_false(conjunction(handles));
          auto verdict = solver();
          if(verdict == decision_proceduret::resultt::D_ERROR)
          {
            p.witness_trace.reset();
            p.failure("solver error checking full-model prefix");
            continue;
          }
          if(verdict == decision_proceduret::resultt::D_SATISFIABLE)
            p.witness_trace = compute_trans_trace(
              handles,
              solver,
              depth + 1,
              ns,
              original_system.main_symbol->name);
          else
            p.witness_trace.reset();
          bad = p.witness_trace ? p.witness_trace->get_min_failing_timeframe()
                                : std::optional<std::size_t>{};
        }
        if(!bad || (bmc_bound && *bad > *bmc_bound))
        {
          p.witness_trace.reset();
          p.inconclusive();
          continue;
        }
        p.witness_trace->loop_start.reset();
        p.witness_trace->total_extension = true;
        p.witness_certificate = certificate::TOTAL_PREFIX;
        messaget{messages}.status()
          << p.name << ": finite bad frame " << *bad
          << " has an infinite extension by totality" << messaget::eom;
        continue;
      }
      need_witness = true;
    }
  }
  if(!need_witness)
    return;

  const auto memory_budget = witness_memory_budget(cmdline);
  const char *reason = "could not start under its address-space limit";
  try
  {
    std::optional<lasso_memory_limitt> memory;
    if(memory_budget)
      memory.emplace(*memory_budget);
    if(!memory || memory->available())
    {
      certify_pending_lassos(
        solver_options,
        original_system,
        result,
        *budget,
        bmc_bound,
        messages,
        observe_lasso);
      return;
    }
  }
  catch(const std::bad_alloc &)
  {
    // Report after restoring the caller's address-space limit.
    reason =
      memory_budget ? "exhausted its memory budget" : "ran out of memory";
  }
  catch(const std::length_error &)
  {
    // A requested horizon can exceed the size of a solver container.
    reason = "exceeded the solver's representable size";
  }
  for(auto &p : result.properties)
    if(needs_lasso(p))
    {
      p.witness_trace.reset();
      p.inconclusive();
      messaget{messages}.status()
        << p.name << ": witness lasso " << reason << messaget::eom;
    }
}
