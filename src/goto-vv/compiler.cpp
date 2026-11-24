#include <slang/ast/types/NetType.h>
#include <slang/util/VersionInfo.h>

#include "lower.h"

#include <iostream>

namespace vv
{
source_locationt compilert::loc(SourceLocation l) const
{
  source_locationt out;
  if(l)
  {
    auto f = sm.getFullyOriginalLoc(l);
    out.set_file(std::string(sm.getFileName(f)));
    out.set_line(sm.getLineNumber(f));
    out.set_column(sm.getColumnNumber(f));
  }
  return out;
}
void compilert::fail(SourceLocation l, const std::string &why) const
{
  auto a = loc(l);
  throw std::runtime_error(
    id2string(a.get_file()) + ":" + id2string(a.get_line()) + ":" +
    id2string(a.get_column()) + ": " + why);
}
irept compilert::describe_type(const Type &t) const
{
  const Type &c = t.getCanonicalType();
  irept desc(std::string(toString(c.kind)));
  desc.set("four_state", t.isFourState());
  if(!t.name.empty())
    desc.set("original_name", t.getHierarchicalPath());
  if(t.location)
    desc.add(ID_C_source_location) = loc(t.location);
  if(c.isIntegral())
  {
    desc.set("width", std::to_string(c.getBitWidth()));
    desc.set("signed", c.isSigned());
  }
  if(c.hasFixedRange())
  {
    auto range = c.getFixedRange();
    desc.set("left", std::to_string(range.left));
    desc.set("right", std::to_string(range.right));
  }
  if(c.kind == SymbolKind::EnumType)
  {
    for(auto &v : c.as<EnumType>().values())
    {
      auto &member = desc.add("members").add(std::string(v.name));
      member.id("enum_member");
      member.add(ID_C_source_location) = loc(v.location);
      member.add(ID_value) = literal(v.getValue().integer(), type(c));
    }
  }
  else if(c.kind == SymbolKind::UnpackedStructType)
  {
    for(auto *field : c.as<UnpackedStructType>().fields)
    {
      irept member("field");
      member.set(ID_name, std::string(field->name));
      member.add(ID_type) = describe_type(field->getType());
      member.add(ID_C_source_location) = loc(field->location);
      desc.get_sub().push_back(std::move(member));
    }
  }
  else if(c.kind == SymbolKind::FixedSizeUnpackedArrayType)
    desc.add("element") =
      describe_type(c.as<FixedSizeUnpackedArrayType>().elementType);
  else if(c.kind == SymbolKind::PackedArrayType)
    desc.add("element") = describe_type(c.as<PackedArrayType>().elementType);
  return desc;
}
typet compilert::type(const Type &t, SourceLocation fallback) const
{
  auto &c = t.getCanonicalType();
  if(c.kind == SymbolKind::FixedSizeUnpackedArrayType)
  {
    auto &a = c.as<FixedSizeUnpackedArrayType>();
    return verilog_array_typet(
      ID_verilog_unpacked_array,
      type(a.elementType),
      a.range.width(),
      a.range.lower(),
      !a.range.isDescending());
  }
  if(c.kind == SymbolKind::UnpackedStructType)
  {
    struct_typet result;
    for(auto *field : c.as<UnpackedStructType>().fields)
      result.components().emplace_back(
        std::string(field->name), type(field->getType()));
    return result;
  }
  if(!c.isIntegral() || !c.getBitWidth())
    fail(
      c.location ? c.location : fallback, "unsupported type " + c.toString());
  auto w = c.getBitWidth();
  if(w == 1 && !c.isSigned())
    return bool_typet();
  return c.isSigned() ? typet(signedbv_typet(w)) : typet(unsignedbv_typet(w));
}
exprt compilert::literal(const SVInt &v, const typet &t) const
{
  if(v.hasUnknown())
  {
    std::string bits;
    for(unsigned i = v.getBitWidth(); i > 0; i--)
      bits += v[int(i - 1)].toChar();
    typet four = v.isSigned()
                   ? typet(verilog_signedbv_typet(v.getBitWidth()))
                   : typet(verilog_unsignedbv_typet(v.getBitWidth()));
    return typecast_exprt::conditional_cast(constant_exprt(bits, four), t);
  }
  mp_integer n = 0;
  for(unsigned i = v.getBitWidth(); i > 0; i--)
  {
    n *= 2;
    if(bool(v[int(i - 1)]))
      n += 1;
  }
  return from_integer(n, t);
}
symbolt &compilert::add(const Symbol &s, const typet &t)
{
  const auto id = name(s);
  symbolt symbol(id, t, ID_Verilog);
  symbol.base_name = std::string(s.name);
  symbol.pretty_name = s.getHierarchicalPath();
  symbol.module = root;
  symbol.location = loc(s.location);
  if(object.symbols.add(symbol))
    fail(s.location, "duplicate symbol " + id);
  return object.symbols.get_writeable_ref(id);
}
void compilert::declare(const ValueSymbol &s, const PortSymbol *p, exprt &items)
{
  if(s.kind == SymbolKind::Net)
  {
    const auto &net = s.as<NetSymbol>();
    auto [drive0, drive1] = net.getDriveStrength();
    if(net.getDelay() || drive0 || drive1)
      fail(s.location, "unsupported delayed or strength-qualified net");
    if(
      net.netType.netKind != NetType::Wire &&
      net.netType.netKind != NetType::UWire)
      fail(s.location, "unsupported net kind " + std::string(net.netType.name));
  }
  const bool string_parameter =
    s.kind == SymbolKind::Parameter && s.getType().isString();
  auto t =
    string_parameter ? typet(ID_verilog_string) : type(s.getType(), s.location);
  auto &symbol = add(s, t);
  auto &info = object.metadata.add("signals").add(symbol.name);
  info.id("declaration");
  info.add("source_type") = describe_type(s.getType());
  info.add(ID_C_source_location) = symbol.location;
  info.set("four_state", s.getType().isFourState());
  info.set("name", s.getHierarchicalPath());
  if(s.kind == SymbolKind::Parameter)
  {
    auto &v = s.as<ParameterSymbol>().getValue();
    symbol.is_macro = true;
    if(string_parameter)
    {
      if(!v.isString())
        fail(s.location, "unsupported nonconstant string parameter");
      symbol.value = constant_exprt(v.str(), t);
    }
    else if(v.isInteger())
      symbol.value = literal(v.integer(), t);
    else
      fail(s.location, "unsupported parameter value");
    return;
  }
  symbol.is_lvalue = s.kind == SymbolKind::Variable;
  if(p)
  {
    if(
      p->direction != ArgumentDirection::In &&
      p->direction != ArgumentDirection::Out)
      fail(p->location, "unsupported port direction");
    symbol.is_input = p->direction == ArgumentDirection::In;
    symbol.is_output = !symbol.is_input;
    if(symbol.is_input)
      symbol.is_lvalue = false;
  }
  const auto *init = s.getInitializer();
  if(s.kind == SymbolKind::Net && s.getType().isIntegral())
  {
    auto width = s.getType().getBitWidth();
    std::string mask(width, init ? '1' : '0');
    for(auto *drv : am.getDrivers(s))
    {
      auto [lo, hi] = drv->getBounds();
      for(auto i = lo; i < width && i <= hi; i++)
        mask[width - 1 - i] = '1';
    }
    info.set("driven_msb_first", mask);
  }
  if(p && !init)
    return;
  exprt decl = item(ID_decl);
  decl.set(ID_class, symbol.is_lvalue ? ID_reg : ID_wire);
  exprt val = node(ID_declarator, t);
  val.set(ID_identifier, symbol.name);
  val.set(ID_base_name, symbol.base_name);
  decl.add_source_location() = symbol.location;
  val.add_source_location() = symbol.location;
  if(init)
  {
    auto v = expr(*init);
    if(s.kind == SymbolKind::Net)
    {
      auto a = item(
        ID_continuous_assign,
        {equal_exprt(symbol.symbol_expr(), std::move(v))});
      a.add_source_location() = loc(init->sourceRange.start());
      items.operands().push_back(a);
    }
    else
      val.add(ID_value) = std::move(v);
  }
  decl.operands().push_back(std::move(val));
  items.operands().push_back(std::move(decl));
}
void compilert::module(const InstanceSymbol &i, bool top)
{
  auto previous_scope = scope;
  scope = &i;
  auto &occurrence = object.metadata.add("instances").add(name(i));
  occurrence.id("instance");
  occurrence.set("path", i.getHierarchicalPath());
  occurrence.set("definition", std::string(i.getDefinition().name));
  occurrence.add("definition_location") = loc(i.getDefinition().location);
  occurrence.add(ID_C_source_location) = loc(i.location);
  auto id = top ? "Verilog::" + std::string(i.name) : name(i) + "$module";
  auto &inst = add(i, verilog_module_instance_typet());
  inst.value = verilog_module_instancet(id);
  module_typet mt;
  exprt items = item(ID_verilog_module);
  std::map<const Symbol *, const PortSymbol *> ports;
  for(auto &s : i.body.members())
    if(s.kind == SymbolKind::Port)
    {
      auto &p = s.as<PortSymbol>();
      if(!p.internalSymbol)
        fail(p.location, "missing internal port");
      ports.emplace(p.internalSymbol, &p);
      mt.ports().emplace_back(
        name(*p.internalSymbol),
        std::string(p.name),
        type(p.getType(), p.location),
        p.direction == ArgumentDirection::In ? ID_input : ID_output);
    }
  members(i.body, ports, items);
  symbolt mod(id, mt, ID_Verilog);
  mod.module = id;
  mod.base_name = std::string(i.getDefinition().name);
  mod.pretty_name = i.getHierarchicalPath();
  mod.value = std::move(items);
  mod.location = loc(i.location);
  scope = previous_scope;
  if(object.symbols.add(mod))
    fail(i.location, "duplicate module " + id);
}
void compilert::members(const Scope &body, const port_mapt &ports, exprt &items)
{
  const auto lower_generate_scope = [&](const GenerateBlockSymbol &block)
  {
    const auto *enclosing = scope;
    scope = &block;
    members(block, {}, items);
    scope = enclosing;
  };

  // Declare before translating initializers and statements. Expression references
  // use the bound slang symbol and its type, including forward references.
  for(auto &s : body.members())
    if(
      s.kind == SymbolKind::Variable || s.kind == SymbolKind::Net ||
      s.kind == SymbolKind::Parameter)
      declare(
        s.as<ValueSymbol>(), ports.count(&s) ? ports.at(&s) : nullptr, items);
  for(auto &s : body.members())
  {
    switch(s.kind)
    {
    case SymbolKind::GenerateBlock:
    {
      auto &block = s.as<GenerateBlockSymbol>();
      if(!block.isUninstantiated)
        lower_generate_scope(block);
      break;
    }
    case SymbolKind::GenerateBlockArray:
      for(auto *entry : s.as<GenerateBlockArraySymbol>().entries)
        lower_generate_scope(*entry);
      break;
    // A called constant function is evaluated in its elaborated instance.
    case SymbolKind::Subroutine:
    case SymbolKind::Genvar:
    case SymbolKind::Port:
    case SymbolKind::Variable:
    case SymbolKind::Parameter:
    case SymbolKind::Net:
    case SymbolKind::StatementBlock:
    case SymbolKind::WildcardImport:
    case SymbolKind::ExplicitImport:
    case SymbolKind::TypeAlias:
    case SymbolKind::EmptyMember:
      break;
    case SymbolKind::TransparentMember:
      // Names hoisted from a local enum refer to its existing value symbols.
      if(s.as<TransparentMemberSymbol>().wrapped.kind != SymbolKind::EnumValue)
        fail(s.location, "unsupported transparent module member");
      break;
    case SymbolKind::Instance:
    {
      auto &child = s.as<InstanceSymbol>();
      auto cid = name(child) + "$module";
      exprt inner = item(ID_inst);
      inner.set(ID_identifier, name(child));
      inner.set(ID_base_name, std::string(child.name));
      inner.set(ID_module_identifier, cid);
      inner.add_source_location() = loc(child.location);
      for(auto *c : child.getPortConnections())
      {
        auto &p = c->port.as<PortSymbol>();
        auto *e = c->getExpression();
        // An open output has a formal and its original driver inside the
        // child, but no receiving expression in the parent.
        if(!e && p.internalSymbol && p.direction == ArgumentDirection::Out)
          continue;
        if(!e || !p.internalSymbol)
          fail(child.location, "unbound child port " + std::string(p.name));
        if(e->kind == ExpressionKind::Assignment)
          e = &e->as<AssignmentExpression>().left();
        auto v = p.direction == ArgumentDirection::In ? expr(*e) : lhs(*e);
        auto formal = symbol_exprt(name(*p.internalSymbol), type(p.getType()));
        if(v.id() == ID_symbol)
          clock_alias[id2string(formal.get_identifier())] =
            id2string(v.get(ID_identifier));
        inner.operands().push_back(
          item(ID_verilog_named_port_connection, {formal, v}));
      }
      module(child, false);
      auto outer = item(ID_inst, {std::move(inner)});
      outer.set(ID_module, std::string(child.getDefinition().name));
      items.operands().push_back(std::move(outer));
      break;
    }
    case SymbolKind::ContinuousAssign:
    {
      auto &ca = s.as<ContinuousAssignSymbol>();
      auto &a = ca.getAssignment().as<AssignmentExpression>();
      const auto strengths = ca.getDriveStrength();
      if(strengths.first || strengths.second)
        fail(s.location, "unsupported driver strength");
      if(ca.getDelay())
        fail(s.location, "delayed continuous assign");
      auto assign = item(
        ID_continuous_assign, {equal_exprt(lhs(a.left()), expr(a.right()))});
      assign.add_source_location() = loc(s.location);
      items.operands().push_back(std::move(assign));
      break;
    }
    case SymbolKind::ProceduralBlock:
    {
      auto &p = s.as<ProceduralBlockSymbol>();
      auto &body = p.getBody();
      if(body.kind == StatementKind::Block)
      {
        auto &block = body.as<BlockStatement>();
        if(
          block.body.kind == StatementKind::ConcurrentAssertion &&
          block.blockSymbol)
        {
          property(
            block.body.as<ConcurrentAssertionStatement>(),
            *block.blockSymbol,
            items);
          break;
        }
      }
      auto kind = toString(p.procedureKind);
      irep_idt op;
      if(kind == "AlwaysFF")
        op = ID_verilog_always_ff;
      else if(kind == "AlwaysComb")
        op = ID_verilog_always_comb;
      else if(kind == "Initial")
        op = ID_initial;
      else if(kind == "Always")
        op = ID_verilog_always;
      else
        fail(s.location, "unsupported procedure " + std::string(kind));
      // An outer @* responds to the implicit combinational sensitivity set.
      // Explicit edge controls retain their resolved signals.
      const auto *timed = body.as_if<TimedStatement>();
      exprt statement;
      in_always_comb = kind == "AlwaysComb";
      if(kind == "Always" && timed &&
         timed->timing.kind == TimingControlKind::ImplicitEvent)
        statement = item(
          ID_event_guard,
          {typecast_exprt(item(ID_verilog_star_event), bool_typet()),
           stmt(timed->stmt)});
      else if(kind == "AlwaysFF" && timed &&
              timed->timing.kind == TimingControlKind::EventList)
        statement = clock_reset_block(*timed);
      else
        statement = stmt(body);
      in_always_comb = false;
      auto process = item(op, {std::move(statement)});
      process.add_source_location() = loc(s.location);
      items.operands().push_back(std::move(process));
      break;
    }
    default:
      fail(
        s.location,
        "unsupported module member " + std::string(toString(s.kind)));
    }
  }
}
} // namespace vv

int compile_design(int argc, char **argv)
{
  using namespace vv;
  if(argc < 4)
  {
    std::cerr << "usage: goto-vv compile OUTPUT [-mfcu|-sfcu] [slang options] "
                 "INPUT...\n";
    return 2;
  }
  try
  {
    if(
      VersionInfo::getMajor() != 11 || VersionInfo::getMinor() != 0 ||
      VersionInfo::getPatch() != 0)
      throw std::runtime_error("goto-vv schema 1 requires slang 11.0.0");
    driver::Driver driver;
    driver.addStandardArgs();
    driver.cmdLine.addRenameCommand("-mfcu,--single-unit");
    driver.cmdLine.addRenameCommand("-sfcu,--single-unit=false");
    if(
      !driver.parseCommandLine(argc - 1, argv + 1) ||
      !driver.processOptions() || !driver.parseAllSources())
      return 2;
    auto c = driver.createCompilation();
    driver.reportCompilation(*c, true);
    if(driver.diagEngine.getNumErrors() != 0)
    {
      (void)driver.reportDiagnostics(true);
      return 2;
    }
    analysis::AnalysisManager am(
      driver.getAnalysisOptions(), driver.threadPool);
    compilert lower(driver.sourceManager, am);
    std::mutex lock;
    am.addListener(
      [&](const analysis::AnalyzedAssertion &a)
      {
        if(
          auto p =
            std::get_if<const ConcurrentAssertionStatement *>(&a.astNode))
        {
          std::lock_guard guard(lock);
          lower.clocks[*p] = a.getSemanticLeadingClock();
        }
      });
    c->freeze();
    am.analyze(*c);
    c->unfreeze();
    driver.diagEngine.issue(am.getDiagnostics());
    if(!driver.reportDiagnostics(true))
      return 2;
    auto tops = c->getRoot().topInstances;
    if(tops.size() != 1)
      throw std::runtime_error("expected a single top");
    auto &top = *tops.front();
    auto &meta = lower.object.metadata;
    meta.set("top", std::string(top.name));
    meta.set("slang_version", VersionInfo::getVersionString());
    auto &options = meta.add("compile_options");
    for(int i = 2; i < argc; i++)
      options.get_sub().emplace_back(argv[i]);
    meta.set("stage", "compiled");
    meta.set("undriven", "seq");
    meta.set("sampling", sampling);
    meta.add(ID_C_source_location) = lower.loc(top.location);
    lower.module(top, true);
    lower.object.validate(false);
    lower.object.write(argv[1]);
    return 0;
  }
  catch(const std::exception &e)
  {
    std::cerr << e.what() << '\n';
    return 2;
  }
}
