#include "vv_object.h"

#include <util/std_expr.h>

#include "verilog_expr.h"
#include "verilog_types.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace
{
const std::string header = "goto-vv 1 slang-11.0.0 ebmc-eae879de";
const std::string clock_gate_header = header + " clock-gate-contract-v1";
const std::string clock_gate_branches_header = clock_gate_header + " branches";
// Older readers must reject a low reset instead of assuming active high.
const std::string low_reset_header = header + " reset-low";
using numbert = std::size_t;
using fields_t = std::map<std::string, const irept *>;

const irept *reset_polarity_field(const irept &metadata)
{
  const auto &fields = metadata.get_named_sub();
  const auto polarity = fields.find("reset_polarity");
  return polarity == fields.end() ? nullptr : &polarity->second;
}

class writert
{
  std::ostream &out;
  std::unordered_map<irept, numbert, irep_full_hash, irep_full_eq> ids;

public:
  explicit writert(std::ostream &out) : out(out)
  {
  }
  numbert node(const irept &n)
  {
    auto seen = ids.find(n);
    if(seen != ids.end())
      return seen->second;
    std::vector<numbert> children;
    std::vector<std::pair<std::string, numbert>> fields;
    for(auto &c : n.get_sub())
      children.push_back(node(c));
    fields_t ordered;
    for(auto &p : n.get_named_sub())
      ordered.emplace(id2string(p.first), &p.second);
    for(auto &p : ordered)
      fields.emplace_back(p.first, node(*p.second));
    auto id = ids.size();
    ids.emplace(n, id);
    out << std::quoted(id2string(n.id())) << ' ' << children.size();
    for(auto c : children)
      out << ' ' << c;
    out << ' ' << fields.size();
    for(auto &p : fields)
      out << ' ' << std::quoted(p.first) << ' ' << p.second;
    out << '\n';
    return id;
  }
};
irept pack(const symbolt &s)
{
  irept r("symbol_record");
  r.set(ID_identifier, s.name);
  r.set(ID_base_name, s.base_name);
  r.set(ID_module, s.module);
  r.set(ID_C_pretty_name, s.pretty_name);
  r.add(ID_type) = s.type;
  r.add(ID_value) = s.value;
  r.add(ID_C_source_location) = s.location;
  r.set("input", s.is_input);
  r.set("output", s.is_output);
  r.set("property", s.is_property);
  r.set("lvalue", s.is_lvalue);
  r.set("macro", s.is_macro);
  r.set("parameter", s.is_parameter);
  r.set("state", s.is_state_var);
  return r;
}
symbolt unpack(const irept &r)
{
  symbolt s(
    r.get(ID_identifier),
    static_cast<const typet &>(r.find(ID_type)),
    ID_Verilog);
  s.base_name = r.get(ID_base_name);
  s.pretty_name = r.get(ID_C_pretty_name);
  s.module = r.get(ID_module);
  s.value = static_cast<const exprt &>(r.find(ID_value));
  s.location =
    static_cast<const source_locationt &>(r.find(ID_C_source_location));
  s.is_input = r.get_bool("input");
  s.is_output = r.get_bool("output");
  s.is_property = r.get_bool("property");
  s.is_lvalue = r.get_bool("lvalue");
  s.is_macro = r.get_bool("macro");
  s.is_parameter = r.get_bool("parameter");
  s.is_state_var = r.get_bool("state");
  return s;
}
[[noreturn]] void error(const source_locationt &loc, const std::string &message)
{
  throw std::runtime_error(
    id2string(loc.get_file()) + ":" + id2string(loc.get_line()) + ": " +
    message);
}
} // namespace
void vv_objectt::write(const std::string &path) const
{
  std::ofstream out(path);
  if(!out)
    throw std::runtime_error("cannot open " + path);
  out << (has_clock_gate_branches()        ? clock_gate_branches_header
          : has_clock_gate()               ? clock_gate_header
          : reset_polarity_field(metadata) ? low_reset_header
                                           : header)
      << '\n';
  irept root("object");
  root.add("metadata") = metadata;
  std::map<std::string, const symbolt *> sorted;
  for(auto &p : symbols.symbols)
    sorted.emplace(id2string(p.first), &p.second);
  for(auto &p : sorted)
    root.get_sub().push_back(pack(*p.second));
  writert(out).node(root);
  out.close();
  if(!out)
    throw std::runtime_error("cannot write " + path);
}
vv_objectt vv_objectt::read(const std::string &path)
{
  std::ifstream in(path);
  std::string line;
  if(
    !std::getline(in, line) ||
    (line != header && line != clock_gate_header &&
     line != clock_gate_branches_header && line != low_reset_header))
    throw std::runtime_error(
      path + ":1: unsupported goto-vv schema or tool versions");
  const bool branches = line == clock_gate_branches_header;
  const bool clock_gate = branches || line == clock_gate_header;
  const bool low_reset = line == low_reset_header;
  std::vector<irept> nodes;
  std::string id;
  auto bad = [&]
  {
    throw std::runtime_error(
      path + ":" + std::to_string(nodes.size() + 2) +
      ": malformed typed object");
  };
  // Counts and references are written as size_t. They are bounded by the
  // data that follows and by the records read so far, not by a fixed size.
  auto number = [&]
  {
    numbert n;
    in >> std::ws;
    if(!std::isdigit(in.peek()) || !(in >> n))
      bad();
    return n;
  };
  auto ref = [&]() -> const irept &
  {
    auto n = number();
    if(n >= nodes.size())
      bad();
    return nodes[n];
  };
  std::vector<std::pair<irep_idt, irept>> fields;
  while(in >> std::ws && in.peek() != EOF)
  {
    if(in.peek() != '"' || !(in >> std::quoted(id)))
      bad();
    irept n(id);
    for(numbert k = number(); k; --k)
      n.get_sub().push_back(ref());
    fields.clear();
    for(numbert k = number(); k; --k)
    {
      std::string key;
      if(!(in >> std::ws) || in.peek() != '"' || !(in >> std::quoted(key)))
        bad();
      fields.emplace_back(key, ref());
    }
    // The writer orders fields by name; named_sub orders them by identifier.
    // Sort once and append, instead of a search per field.
    std::sort(
      fields.begin(),
      fields.end(),
      [](const auto &a, const auto &b) { return a.first < b.first; });
    if(
      std::adjacent_find(
        fields.begin(),
        fields.end(),
        [](const auto &a, const auto &b) { return a.first == b.first; }) !=
      fields.end())
      bad();
    auto &named = n.get_named_sub();
#if NAMED_SUB_IS_FORWARD_LIST
    auto tail = named.before_begin();
    for(auto &f : fields)
      tail = named.emplace_after(tail, f.first, std::move(f.second));
#else
    for(auto &f : fields)
      named.emplace_hint(named.end(), f.first, std::move(f.second));
#endif
    nodes.push_back(std::move(n));
  }
  if(in.bad() || nodes.empty() || nodes.back().id() != "object")
    bad();
  vv_objectt result;
  result.metadata = nodes.back().find("metadata");
  for(const auto &r : nodes.back().get_sub())
    if(r.id() != "symbol_record" || result.symbols.add(unpack(r)))
      bad();
  if(
    result.has_clock_gate() != clock_gate ||
    result.has_clock_gate_branches() != branches ||
    (reset_polarity_field(result.metadata) != nullptr) != low_reset)
    bad();
  return result;
}

std::string vv_objectt::clock_gate_description() const
{
  const auto &gate = metadata.find("clock_gate_contract");
  auto result =
    id2string(gate.id()) + " (temporary) ENV1 " + id2string(gate.get("ENV1")) +
    "=0 | ENV2 " + id2string(gate.get("ENV2")) + " changes only at " +
    id2string(gate.get("master")) +
    " rising boundaries, at least two low samples | " +
    id2string(gate.get("sampling")) + " | gate " + id2string(gate.get("gate")) +
    " | RstSync " + id2string(gate.get("RstSync")) + " | reset mux " +
    id2string(gate.get("reset_mux")) +
    (metadata.get_bool("clock_gate_start_reset") ? " | initial low required"
                                                 : "");
  for(const auto &[path, branch] : gate.find("ClockGate").get_named_sub())
    result += " | ClockGate " + id2string(path) + " input " +
              id2string(branch.get("input")) + " output " +
              id2string(branch.get("output")) + " enable " +
              id2string(branch.get("enable")) + " effective " +
              id2string(branch.get("effective_enable")) + " low-phase/preponed";
  for(const auto &[signal, policy] :
      gate.find("independent_resets").get_named_sub())
    result += " | independent reset " + id2string(signal) + " " +
              id2string(policy.id());
  return result;
}

void vv_objectt::validate(bool require_constraints) const
{
  auto loc =
    static_cast<const source_locationt &>(metadata.find(ID_C_source_location));
  if(
    metadata.get("sampling") != "single-posedge/preponed/disable-current" ||
    (metadata.get("undriven") != "seq" && metadata.get("undriven") != "const") ||
    (require_constraints && metadata.get("stage") != "constrained"))
    error(loc, "unsupported sampling model, floating policy, or object stage");
  std::string top = id2string(metadata.get("top"));
  auto module = symbols.lookup("Verilog::" + top);
  if(
    !module || module->type.id() != ID_module ||
    module->value.id() != ID_verilog_module)
    error(loc, "missing typed top module " + top);
  auto clock = symbols.lookup(metadata.get("clock"));
  const auto &ports = to_module_type(module->type).ports();
  auto primary_input = [&](const symbolt *s)
  {
    return s && s->is_input && s->type.id() == ID_bool &&
           std::any_of(
             ports.begin(),
             ports.end(),
             [&](const auto &p)
             { return p.input() && !p.output() && p.identifier() == s->name; });
  };
  if(!primary_input(clock))
    error(loc, "unbound primary clock");
  const auto reset = metadata.get("reset");
  if(const auto *polarity = reset_polarity_field(metadata))
  {
    if(reset.empty())
      error(loc, "reset polarity requires a reset binding");
    // The legacy encoding omits the polarity and means active high.
    if(polarity->id() != "low")
      error(loc, "invalid reset polarity");
  }
  if(!reset.empty())
  {
    auto r = symbols.lookup("Verilog::$root." + id2string(reset));
    if(!primary_input(r))
      error(loc, "invalid reset binding");
  }
  if(has_clock_gate())
  {
    const auto &gate = metadata.find("clock_gate_contract");
    auto stage = metadata.get("stage");
    const auto *gate_net = symbols.lookup(gate.get("gate"));
    const auto *mux = symbols.lookup(gate.get("reset_mux"));
    const auto *due = symbols.lookup(gate.get("ENV2_due"));
    const bool initial = metadata.get_bool("clock_gate_start_reset");
    if(
      gate.id() != clock_gate_v1 ||
      gate.get("sampling") != clock_gate_sampling ||
      gate.get("master") != clock->name ||
      !primary_input(symbols.lookup(gate.get("ENV1"))) ||
      !primary_input(symbols.lookup(gate.get("ENV2"))) ||
      gate.get("ENV1") == gate.get("ENV2") || gate.get("ENV1") == clock->name ||
      gate.get("ENV2") == clock->name || !gate_net || !mux ||
      gate_net->type.id() != ID_bool || mux->type.id() != ID_bool || !due ||
      due->type.id() != ID_bool || !reset.empty() ||
      gate.get("RstSync").empty() ||
      (stage != "clock-gate-compiled" && stage != "constrained") ||
      (initial && stage != "constrained"))
      error(loc, "invalid clock gate contract or conflicting reset");
    const auto &branches = gate.find("ClockGate").get_named_sub();
    std::map<irep_idt, irep_idt> branch_sources;
    auto scalar = [&](const irep_idt &id)
    {
      const auto *s = symbols.lookup(id);
      return s && s->type.id() == ID_bool;
    };
    for(const auto &[path, branch] : branches)
    {
      const auto id = "Verilog::$root." + id2string(path);
      auto output = branch.get("output");
      auto input = branch.get("input");
      if(
        branch.id() != "low-phase-preponed" || !symbols.lookup(id) ||
        output == clock->name || output == gate.get("gate") ||
        !scalar(output) || !scalar(input) || !scalar(branch.get("enable")) ||
        !scalar(branch.get("effective_enable")) ||
        !branch_sources.emplace(output, input).second)
        error(loc, "invalid ClockGate branch " + id2string(path));
    }
    for(const auto &[output, input] : branch_sources)
    {
      std::set<irep_idt> seen{output};
      auto parent = input;
      while(parent != clock->name && parent != gate.get("gate"))
      {
        const auto earlier = branch_sources.find(parent);
        if(earlier == branch_sources.end() || !seen.insert(parent).second)
          error(
            loc,
            "ClockGate branch outside the root domain " + id2string(output));
        parent = earlier->second;
      }
    }
    for(const auto &[signal, policy] :
        gate.find("independent_resets").get_named_sub())
      if(
        branches.empty() || !scalar(signal) || signal == gate.get("ENV2") ||
        policy.id() != clock_gate_independent_reset_sampling)
        error(loc, "invalid independent ClockGate reset " + id2string(signal));
    for(const auto &label : {"clock_gate_ENV1", "clock_gate_ENV2"})
    {
      const auto id = "Verilog::$root." + top + "." + label;
      const auto *property = symbols.lookup(id);
      if(
        !property || !property->is_property ||
        metadata.find("properties").find(id).get("kind") !=
          ID_verilog_assume_property)
        error(loc, "missing clock gate environment assumption " + id);
    }
  }
  else if(
    metadata.get("stage") == "clock-gate-compiled" ||
    metadata.get_bool("clock_gate_start_reset"))
    error(loc, "missing clock gate contract");
  std::map<irep_idt, irep_idt> connections;
  std::vector<std::pair<irep_idt, source_locationt>> edges;
  std::set<irep_idt> bound_properties;
  std::map<irep_idt, std::size_t> diagnostic_calls;
  for(auto &entry : symbols.symbols)
  {
    const auto &s = entry.second;
    if(s.is_property)
    {
      const auto &p = metadata.find("properties").find(s.name);
      if(p.get("kind") == ID_verilog_immediate_assert)
      {
        if(
          p.get("sampling") != "procedural" || !p.get("clock").empty() ||
          !p.get("edge").empty())
          error(s.location, "invalid procedural assertion sampling");
      }
      else if(
        p.is_nil() || p.get("clock") != clock->name ||
        p.get("edge") != "posedge" ||
        p.get("sampling") != metadata.get("sampling"))
        error(s.location, "unverified clock for " + id2string(s.name));
    }
    const bool function_diagnostics =
      s.type.id() == ID_code && s.type.get_bool("#vv_function_diagnostics");
    if(s.value.id() != ID_verilog_module && !function_diagnostics)
      continue;
    if(function_diagnostics && s.value.id() != ID_block)
      error(s.location, "invalid runtime diagnostic function");
    auto check = [&](auto &&self, const irept &n) -> void
    {
      const auto &where =
        static_cast<const source_locationt &>(n.find(ID_C_source_location));
      if(n.id() == ID_inst && !n.get(ID_module_identifier).empty())
      {
        const auto *inst = symbols.lookup(n.get(ID_identifier));
        const auto *child = symbols.lookup(n.get(ID_module_identifier));
        if(
          !inst || inst->value.id() != ID_verilog_module_instance || !child ||
          child->type.id() != ID_module ||
          child->value.id() != ID_verilog_module ||
          inst->value.get(ID_module) != child->name)
          error(where, "bad specialized instance binding");
        for(const auto &p : n.get_sub())
        {
          if(
            p.id() != ID_verilog_named_port_connection ||
            p.get_sub().size() != 2 || p.get_sub().front().id() != ID_symbol)
            error(where, "bad formal port binding");
          const auto id = p.get_sub().front().get(ID_identifier);
          const auto &child_ports = to_module_type(child->type).ports();
          if(std::none_of(
               child_ports.begin(),
               child_ports.end(),
               [&](const auto &port) { return port.identifier() == id; }))
            error(where, "formal port does not belong to the bound instance");
          if(p.get_sub().back().id() == ID_symbol)
            connections.emplace(id, p.get_sub().back().get(ID_identifier));
        }
      }
      // A canonical clock/reset process has two event operands. Only its
      // first edge is a sampling clock. Check the reset against the outer
      // if, then visit its signal and the entire body normally.
      if(
        (n.id() == ID_verilog_always || n.id() == ID_verilog_always_ff) &&
        n.get_sub().size() == 1)
      {
        const auto &guard = n.get_sub()[0];
        if(guard.id() == ID_event_guard && guard.get_sub().size() == 2)
        {
          const auto &event = guard.get_sub()[0];
          const auto &body = guard.get_sub()[1];
          if(event.id() == ID_verilog_event && event.get_sub().size() == 2)
          {
            const auto &clk = event.get_sub()[0];
            const auto &reset = event.get_sub()[1];
            const auto &guard_location = static_cast<const source_locationt &>(
              guard.find(ID_C_source_location));
            auto invalid_reset = [&]
            {
              error(
                guard_location.is_nil() ? s.location : guard_location,
                n.id() == ID_verilog_always
                  ? "invalid clock/reset guard in always"
                  : "invalid clock/reset guard in always_ff");
            };
            const bool active_low = reset.id() == ID_negedge;
            if(
              clk.id() != ID_posedge ||
              (!active_low && reset.id() != ID_posedge) ||
              reset.get_sub().size() != 1 ||
              reset.get_sub()[0].id() != ID_symbol ||
              reset.get_sub()[0].find(ID_type).id() != ID_bool ||
              body.id() != ID_if || body.get_sub().size() < 2)
              invalid_reset();

            const auto *tested_signal = &body.get_sub()[0];
            if(active_low)
            {
              if(
                tested_signal->id() != ID_not ||
                tested_signal->find(ID_type).id() != ID_bool ||
                tested_signal->get_sub().size() != 1)
                invalid_reset();
              tested_signal = &tested_signal->get_sub()[0];
            }
            if(
              tested_signal->id() != ID_symbol ||
              tested_signal->get(ID_identifier) !=
                reset.get_sub()[0].get(ID_identifier))
              invalid_reset();
            self(self, clk);
            self(self, reset.get_sub()[0]);
            self(self, body);
            return;
          }
        }
      }
      if(n.id() == ID_negedge)
        error(
          where.is_nil() ? s.location : where,
          "unsupported negative clock edge");
      if(n.id() == ID_posedge)
      {
        if(n.get_sub().size() != 1 || n.get_sub().front().id() != ID_symbol)
          error(
            where.is_nil() ? s.location : where,
            "unsupported clock event expression");
        edges.emplace_back(n.get_sub().front().get(ID_identifier), s.location);
      }
      if(n.id() == ID_symbol)
      {
        auto target = symbols.lookup(n.get(ID_identifier));
        if(!target || target->type != n.find(ID_type))
          error(
            where.is_nil() ? s.location : where,
            "bad signal binding " + id2string(n.get(ID_identifier)));
      }
      if(
        n.id() == ID_function_call && n.get_sub().size() == 2 &&
        n.get_sub().front().id() == ID_symbol)
      {
        const auto *callee =
          symbols.lookup(n.get_sub().front().get(ID_identifier));
        if(callee && callee->type.get_bool("#vv_function_diagnostics"))
          ++diagnostic_calls[callee->name];
      }
      if(
        n.id() == ID_verilog_assert_property ||
        n.id() == ID_verilog_assume_property ||
        n.id() == ID_verilog_cover_property ||
        n.id() == ID_verilog_immediate_assert)
      {
        auto target = symbols.lookup(n.get(ID_identifier));
        const auto &property =
          metadata.find("properties").find(n.get(ID_identifier));
        if(
          !target || !target->is_property || n.get_sub().size() != 2 ||
          property.get("kind") != n.id() ||
          !bound_properties.insert(target->name).second)
          error(where, "bad property binding");
        if(
          function_diagnostics &&
          (n.id() != ID_verilog_immediate_assert ||
           property.get("origin") != "function-diagnostic-task" ||
           property.get("function") != s.name ||
           property.find("function_location").is_nil() ||
           property.find("inner_call_location").is_nil()))
          error(where, "bad runtime diagnostic property binding");
      }
      for(const auto key :
          {verilog_case_statement_baset::overlap_check,
           verilog_case_statement_baset::coverage_check})
      {
        const auto &check = n.find(key);
        if(check.is_nil())
          continue;
        if(
          n.id() != ID_verilog_case ||
          check.id() != ID_verilog_immediate_assert ||
          metadata.find("properties")
              .find(check.get(ID_identifier))
              .get("origin") != "case-qualifier")
          error(where, "invalid case qualifier assertion");
        self(self, check);
      }
      for(const auto &c : n.get_sub())
        self(self, c);
    };
    check(check, s.value);
  }
  for(const auto &[id, s] : symbols.symbols)
  {
    if(s.is_property && !bound_properties.count(id))
      error(s.location, "property symbol without a module item");
    if(s.type.get_bool("#vv_function_diagnostics") && diagnostic_calls[id] != 1)
      error(s.location, "runtime diagnostic function requires one bound call");
  }
  for(auto &edge : edges)
  {
    std::set<irep_idt> seen;
    auto id = edge.first;
    while(id != clock->name)
    {
      auto it = connections.find(id);
      if(!seen.insert(id).second || it == connections.end())
        error(
          edge.second,
          "process clock does not bind to the recorded primary input");
      id = it->second;
    }
  }
}
