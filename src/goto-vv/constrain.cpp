#include <util/arith_tools.h>
#include <util/std_expr.h>

#include <verilog/verilog_expr.h>
#include <verilog/vv_object.h>

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
void apply(vv_objectt &o, const std::string &path)
{
  std::ifstream file(path);
  if(!file)
    throw std::runtime_error("cannot read " + path);
  std::string line;
  unsigned row = 0;
  while(std::getline(file, line))
  {
    ++row;
    std::istringstream in(line);
    std::string op;
    if(!(in >> op) || op[0] == '#')
      continue;
    source_locationt loc;
    loc.set_file(path);
    loc.set_line(row);
    auto reject = [&](std::string reason) {
      throw std::runtime_error(
        path + ":" + std::to_string(row) + ": " + reason);
    };
    auto signal = [&](std::string n) -> const symbolt &
    {
      auto p = o.symbols.lookup("Verilog::$root." + n);
      if(
        !p || !p->is_input || p->type.id() == ID_module ||
        p->module != "Verilog::$root")
        reject("unbound input " + n);
      return *p;
    };
    auto top = id2string(o.metadata.get("top"));
    std::string rest;
    if(op == "undriven")
    {
      std::string policy;
      if(
        !(in >> policy) || in >> rest ||
        (policy != "seq" && policy != "const") ||
        !o.metadata.find("undriven_location").is_nil())
        reject("expected a single undriven seq or undriven const directive");
      o.metadata.set("undriven", policy);
      o.metadata.add("undriven_location") = loc;
      continue;
    }
    if(op == "clock-gate-start-reset")
    {
      if(
        !o.has_clock_gate() || in >> rest ||
        o.metadata.get_bool("clock_gate_start_reset"))
        reject("clock-gate-start-reset requires one clock gate contract");
      o.metadata.set("clock_gate_start_reset", true);
      continue;
    }
    if(op == "reset")
    {
      if(o.has_clock_gate())
        reject(
          "single-cycle reset conflicts with ENV2; use clock-gate-start-reset");
      std::string n;
      if(
        !(in >> n) || in >> rest || signal(n).type.id() != ID_bool ||
        !o.metadata.get("reset").empty())
        reject("invalid initial reset");
      o.metadata.set("reset", n);
      o.metadata.add("reset_location") = loc;
      continue;
    }
    std::string label, edge, clock, n, eq, value;
    if(
      op != "assume" || !(in >> label >> edge >> clock >> n >> eq >> value) ||
      in >> rest || edge != "posedge" || eq != "==" || value != "'0")
      reject("expected assume LABEL posedge CLOCK INPUT == '0");
    if(
      label.empty() ||
      !(std::isalpha(static_cast<unsigned char>(label[0])) ||
        label[0] == '_') ||
      label.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
        std::string::npos)
      reject("invalid label");
    const auto &clk = signal(clock);
    const auto &target = signal(n);
    if(clk.name != o.metadata.get("clock"))
      reject("constraint uses a different clock");
    if(
      target.type.id() != ID_bool && target.type.id() != ID_unsignedbv &&
      target.type.id() != ID_signedbv)
      reject("expected an integral input");
    std::string pretty = top + "." + label, id = "Verilog::$root." + pretty;
    symbolt s(id, bool_typet(), ID_Verilog);
    s.pretty_name = pretty;
    s.base_name = label;
    s.module = "Verilog::$root";
    s.is_property = true;
    s.location = loc;
    if(o.symbols.add(s))
      reject("duplicate label " + label);
    auto &module = o.symbols.get_writeable_ref("Verilog::" + top).value;
    exprt entry(ID_verilog_assume_property, typet(ID_nil));
    entry.set(ID_identifier, id);
    entry.set(ID_base_name, label);
    entry.add_source_location() = loc;
    exprt action(ID_verilog_action_then, typet(ID_nil));
    action.operands().emplace_back(ID_skip, typet(ID_nil));
    equal_exprt predicate(target.symbol_expr(), from_integer(0, target.type));
    predicate.add_source_location() = loc;
    entry.operands() = {std::move(predicate), std::move(action)};
    module.operands().push_back(entry);
    auto &info = o.metadata.add("properties").add(id);
    info.id("property");
    info.set("origin", "environment");
    info.set("clock", clk.name);
    info.set("sampling", o.metadata.get("sampling"));
    info.set("edge", "posedge");
    info.set("kind", entry.id());
    info.add(ID_C_source_location) = loc;
  }
}
} // namespace
int constrain_design(int argc, char **argv)
{
  if(argc != 4)
  {
    std::cerr
      << "usage: goto-vv constrain compiled.vv contract.txt output.vv\n";
    return 2;
  }
  try
  {
    auto o = vv_objectt::read(argv[1]);
    o.validate(false);
    if(
      o.metadata.get("stage") !=
      (o.has_clock_gate() ? "clock-gate-compiled" : "compiled"))
      throw std::runtime_error("expected a compiled object");
    apply(o, argv[2]);
    o.metadata.set("stage", "constrained");
    o.validate(true);
    o.write(argv[3]);
    return 0;
  }
  catch(const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
