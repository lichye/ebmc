#include "vv_object.h"

#include <util/std_expr.h>

#include "verilog_types.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace
{
const std::string header = "goto-vv 1 slang-11.0.0 ebmc-eae879de";
using numbert = std::size_t;
using fields_t = std::map<std::string, const irept *>;

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
  out << header << '\n';
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
  if(!std::getline(in, line) || line != header)
    throw std::runtime_error(
      path + ":1: unsupported goto-vv schema or tool versions");
  std::vector<irept> nodes;
  std::string id;
  auto bad = [&]
  {
    throw std::runtime_error(
      path + ":" + std::to_string(nodes.size() + 2) +
      ": malformed typed object");
  };
  auto count = [&]
  {
    numbert n;
    if(!(in >> n) || n > 2000000)
      bad();
    return n;
  };
  auto ref = [&]() -> const irept &
  {
    auto n = count();
    if(n >= nodes.size())
      bad();
    return nodes[n];
  };
  while(in >> std::ws && in.peek() != EOF)
  {
    if(in.peek() != '"' || !(in >> std::quoted(id)) || nodes.size() >= 2000000)
      bad();
    irept n(id);
    for(numbert k = count(); k; --k)
      n.get_sub().push_back(ref());
    for(numbert k = count(); k; --k)
    {
      std::string key;
      if(
        !(in >> std::quoted(key)) ||
        n.get_named_sub().find(key) != n.get_named_sub().end())
        bad();
      n.add(key) = ref();
    }
    nodes.push_back(std::move(n));
  }
  if(nodes.empty() || nodes.back().id() != "object")
    bad();
  vv_objectt result;
  result.metadata = nodes.back().find("metadata");
  for(const auto &r : nodes.back().get_sub())
    if(r.id() != "symbol_record" || result.symbols.add(unpack(r)))
      bad();
  return result;
}
void vv_objectt::validate(bool require_constraints) const
{
  auto loc =
    static_cast<const source_locationt &>(metadata.find(ID_C_source_location));
  if(
    metadata.get("sampling") != "single-posedge/preponed/disable-current" ||
    metadata.get("undriven") != "seq" ||
    (require_constraints && metadata.get("stage") != "constrained"))
    error(loc, "unsupported sampling model, floating policy, or object stage");
  std::string top = id2string(metadata.get("top"));
  auto module = symbols.lookup("Verilog::" + top);
  if(
    !module || module->type.id() != ID_module ||
    module->value.id() != ID_verilog_module)
    error(loc, "missing typed top module " + top);
  auto clock = symbols.lookup(metadata.get("clock"));
  if(!clock || clock->type.id() != ID_bool || !clock->is_input)
    error(loc, "unbound primary clock");
  if(!metadata.get("reset").empty())
  {
    auto r =
      symbols.lookup("Verilog::$root." + id2string(metadata.get("reset")));
    if(!r || r->type.id() != ID_bool || !r->is_input)
      error(loc, "invalid reset binding");
  }
  for(auto &entry : symbols.symbols)
  {
    const auto &s = entry.second;
    if(s.is_property)
    {
      const auto &p = metadata.find("properties").find(s.name);
      if(
        p.is_nil() || p.get("clock") != clock->name ||
        p.get("edge") != "posedge" ||
        p.get("sampling") != metadata.get("sampling"))
        error(s.location, "unverified clock for " + id2string(s.name));
    }
    if(s.value.id() != ID_verilog_module)
      continue;
    auto check = [&](auto &&self, const irept &n) -> void
    {
      const auto &where =
        static_cast<const source_locationt &>(n.find(ID_C_source_location));
      if(n.id() == ID_symbol)
      {
        auto target = symbols.lookup(n.get(ID_identifier));
        if(!target || target->type != n.find(ID_type))
          error(
            where.is_nil() ? s.location : where,
            "bad signal binding " + id2string(n.get(ID_identifier)));
      }
      if(
        n.id() == ID_verilog_assert_property ||
        n.id() == ID_verilog_assume_property ||
        n.id() == ID_verilog_cover_property)
      {
        auto target = symbols.lookup(n.get(ID_identifier));
        if(!target || !target->is_property || n.get_sub().size() != 2)
          error(where, "bad property binding");
      }
      for(const auto &c : n.get_sub())
        self(self, c);
    };
    check(check, s.value);
  }
}
