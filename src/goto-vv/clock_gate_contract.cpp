#include <util/bitvector_expr.h>
#include <util/namespace.h>
#include <util/simplify_expr.h>

#include <slang/ast/ASTVisitor.h>
#include <slang/ast/expressions/Operator.h>
#include <slang/syntax/SyntaxTree.h>

#include "lower.h"

#include <fstream>
#include <functional>
#include <sstream>

namespace vv
{
namespace
{
const ValueSymbol *named_value(const Expression *expr)
{
  if(!expr || expr->kind != ExpressionKind::NamedValue)
    return nullptr;
  return &expr->as<NamedValueExpression>().symbol;
}

bool has_active_definition(const Symbol &symbol, std::string_view expected)
{
  const auto *definition = symbol.getSyntax();
  if(!definition)
    return false;
  const auto parsed = syntax::SyntaxTree::fromText(expected);
  const auto &root = parsed->root();
  if(root.kind == syntax::SyntaxKind::CompilationUnit)
  {
    const auto &unit = root.as<syntax::CompilationUnitSyntax>();
    return unit.members.size() == 1 &&
           definition->isEquivalentTo(*unit.members[0]);
  }
  return definition->isEquivalentTo(root);
}

// Compare the complete active syntax, excluding comments and inactive
// preprocessor branches. Every delay or stateful statement is significant.
constexpr auto rst_sync_v1 =
  R"sv(module RstSync(input clk_i, input rstn_i, input clk_en, input te,
               output clk_o, output rstn_o);
  localparam RST_DELAY = 2;
  localparam CLK_DELAY = 2;
  logic [RST_DELAY + CLK_DELAY - 1 : 0] rst_delay_reg;
  always_ff @(posedge clk_i or negedge rstn_i) begin
    if (~rstn_i)
      rst_delay_reg <= '0;
    else
      rst_delay_reg <= {rst_delay_reg[RST_DELAY + CLK_DELAY - 2 : 0], 1'b1};
  end
  assign rstn_o = rst_delay_reg[RST_DELAY - 1];
  logic clk_en_int;
  assign clk_en_int = clk_en & rst_delay_reg[CLK_DELAY + RST_DELAY - 1];
  ClockGate icg(.clk_i(clk_i), .enable(clk_en_int), .te(te), .clk_o(clk_o));
endmodule)sv";

constexpr auto clock_gate_v1 =
  R"sv(module ClockGate(input clk_i, input enable, input te, output clk_o);
  logic en_latch;
  always_latch begin
    if (!clk_i) begin
      en_latch = enable | te;
    end
  end
  assign clk_o = en_latch & clk_i;
endmodule)sv";

const Expression *port_expression(const PortConnection &connection)
{
  auto *expression = connection.getExpression();
  if(const auto *assign =
       expression ? expression->as_if<AssignmentExpression>() : nullptr;
     assign && assign->isLValueArg())
    expression = &assign->left();
  return expression;
}

void collect_instances(
  const Scope &scope,
  std::vector<const InstanceSymbol *> &instances,
  std::map<std::string, const ValueSymbol *> &signals)
{
  for(const auto &member : scope.members())
  {
    if(member.kind == SymbolKind::Net || member.kind == SymbolKind::Variable)
      signals.emplace(name(member), &member.as<ValueSymbol>());
    else if(const auto *inst = member.as_if<InstanceSymbol>())
    {
      instances.push_back(inst);
      collect_instances(inst->body, instances, signals);
    }
    else if(const auto *gen = member.as_if<GenerateBlockSymbol>())
    {
      if(!gen->isUninstantiated)
        collect_instances(*gen, instances, signals);
    }
    else if(const auto *array = member.as_if<GenerateBlockArraySymbol>())
    {
      for(const auto *entry : array->entries)
        if(!entry->isUninstantiated)
          collect_instances(*entry, instances, signals);
    }
  }
}

const SignalEventControl *first_event(const TimedStatement &timed)
{
  if(const auto *list = timed.timing.as_if<EventListControl>())
    return list->events.empty()
             ? nullptr
             : list->events.front()->as_if<SignalEventControl>();
  return timed.timing.as_if<SignalEventControl>();
}

const AssignmentExpression *
reset_assignment(const Statement &statement, const ValueSymbol &target)
{
  if(const auto *block = statement.as_if<BlockStatement>())
    return reset_assignment(block->body, target);
  if(const auto *list = statement.as_if<StatementList>())
  {
    const AssignmentExpression *found = nullptr;
    for(const auto *child : list->list)
    {
      const auto *next = reset_assignment(*child, target);
      if(next && found)
        return nullptr;
      if(next)
        found = next;
    }
    return found;
  }
  if(const auto *expr = statement.as_if<ExpressionStatement>())
    if(const auto *assignment = expr->expr.as_if<AssignmentExpression>();
       assignment && assignment->isNonBlocking() &&
       named_value(&assignment->left()) == &target)
      return assignment;
  return nullptr;
}

exprt assign(const exprt &lhs, const exprt &rhs)
{
  return item(ID_continuous_assign, {equal_exprt{lhs, rhs}});
}

exprt update(const exprt &lhs, const exprt &rhs)
{
  return item(ID_verilog_non_blocking_assign, {lhs, rhs});
}

exprt root_edge(const std::string &clock, exprt body)
{
  return item(
    ID_event_guard,
    {node(ID_posedge, bool_typet(), {symbol_exprt{clock, bool_typet()}}),
     std::move(body)});
}

symbol_exprt add_local(
  vv_objectt &object,
  const std::string &id,
  const typet &type,
  const source_locationt &location)
{
  symbolt symbol{id, type, ID_Verilog};
  symbol.module = root;
  symbol.base_name = id.substr(id.rfind('.') + 1);
  symbol.pretty_name = id.substr(std::string(root).size() + 1);
  symbol.location = location;
  symbol.is_lvalue = true;
  if(object.symbols.add(symbol))
    throw std::runtime_error("duplicate generated clock gate symbol " + id);
  return symbol.symbol_expr();
}

void add_environment_assume(
  vv_objectt &object,
  exprt &items,
  const std::string &label,
  const std::string &clock,
  exprt predicate,
  const source_locationt &where)
{
  auto pretty = id2string(object.metadata.get("top")) + "." + label;
  auto id = std::string(root) + "." + pretty;
  symbolt property{id, bool_typet(), ID_Verilog};
  property.pretty_name = pretty;
  property.base_name = label;
  property.location = where;
  property.module = root;
  property.is_property = true;
  if(object.symbols.add(property))
    throw std::runtime_error("duplicate clock gate property " + pretty);
  auto entry = item(
    ID_verilog_assume_property,
    {std::move(predicate), item(ID_verilog_action_then, {item(ID_skip)})});
  entry.set(ID_identifier, id);
  entry.set(ID_base_name, label);
  entry.add_source_location() = where;
  items.operands().push_back(std::move(entry));
  auto &info = object.metadata.add("properties").add(id);
  info.id("property");
  info.set("origin", "temporary clock gate contract");
  info.set("clock", clock);
  info.set("sampling", sampling);
  info.set("edge", "posedge");
  info.set("kind", ID_verilog_assume_property);
  info.add(ID_C_source_location) = where;
}
} // namespace

std::string compilert::connected_signal(std::string id) const
{
  std::set<std::string> seen;
  while(gate_connections.count(id))
  {
    if(!seen.insert(id).second)
      throw std::runtime_error("cycle in clock gate contract port bindings");
    id = gate_connections.at(id);
  }
  return id;
}

std::string compilert::clock_gate_enable(const std::string &output) const
{
  const auto &gate = *clock_gate;
  if(output == gate.output)
    return gate.enable;
  const auto branch = gate.branches.find(output);
  return branch == gate.branches.end() ? std::string{} : branch->second.enable;
}

std::string compilert::gated_clock_enable(const Expression &expression) const
{
  if(
    !clock_gate || (expression.kind != ExpressionKind::NamedValue &&
                    expression.kind != ExpressionKind::HierarchicalValue))
    return {};
  const auto *symbol = expression.getSymbolReference();
  return symbol ? clock_gate_enable(connected_signal(name(*symbol)))
                : std::string{};
}

bool compilert::gated_clock(const Expression &expression) const
{
  return !gated_clock_enable(expression).empty();
}

const Expression *
compilert::clock_gate_value_source(const ValueSymbol &value) const
{
  const auto &gate = *clock_gate;
  const auto drivers = am.getDrivers(value);
  const auto id = name(value);
  if(const auto input = gate.input_values.find(id);
     input != gate.input_values.end())
  {
    // Use the actual connection from this occurrence. Slang may analyze the
    // module body through an identical canonical occurrence, so also exclude
    // any internal driver recorded there.
    auto only_port_driver = [](const auto &drivers)
    {
      return std::all_of(
        drivers.begin(),
        drivers.end(),
        [](const auto *driver) { return driver->isInputPort(); });
    };
    if(!only_port_driver(drivers))
      return nullptr;
    const auto *scope = value.getParentScope();
    const auto *body =
      scope ? scope->asSymbol().as_if<InstanceBodySymbol>() : nullptr;
    const auto *canonical = body && body->parentInstance
                              ? body->parentInstance->getCanonicalBody()
                              : nullptr;
    if(canonical)
    {
      const auto *same = canonical->find(value.name);
      const auto *same_value = same ? same->as_if<ValueSymbol>() : nullptr;
      if(!same_value || !only_port_driver(am.getDrivers(*same_value)))
        return nullptr;
    }
    return input->second;
  }
  if(drivers.size() != 1)
    return nullptr;
  if(value.kind == SymbolKind::Net && value.getInitializer())
    return value.getInitializer();
  const auto *assign =
    drivers.front()->containingSymbol->as_if<ContinuousAssignSymbol>();
  if(!assign || assign->getDelay())
    return nullptr;
  const auto *assignment =
    assign->getAssignment().as_if<AssignmentExpression>();
  return assignment && named_value(&assignment->left()) == &value
           ? &assignment->right()
           : nullptr;
}

const ProceduralBlockSymbol *
compilert::clock_gate_register_driver(const ValueSymbol &value) const
{
  if(value.kind != SymbolKind::Variable)
    return nullptr;
  if(const auto *initial = value.getInitializer())
  {
    EvalContext context(value);
    const auto constant = initial->eval(context);
    if(!constant.isInteger() || constant.integer().hasUnknown())
      return nullptr;
  }
  const ProceduralBlockSymbol *process = nullptr;
  for(const auto *driver : am.getDrivers(value))
  {
    if(
      value.getInitializer() &&
      driver->flags.has(analysis::DriverFlags::Initializer))
      continue;
    const auto *next = driver->containingSymbol->as_if<ProceduralBlockSymbol>();
    if(!next || (process && next != process))
      return nullptr;
    process = next;
  }
  if(
    !process || (process->procedureKind != ProceduralBlockKind::Always &&
                 process->procedureKind != ProceduralBlockKind::AlwaysFF))
    return nullptr;
  const auto *timed = process->getBody().as_if<TimedStatement>();
  const auto *clock = timed ? first_event(*timed) : nullptr;
  const auto *clock_signal = clock ? named_value(&clock->expr) : nullptr;
  if(!clock_signal || clock->edge != EdgeKind::PosEdge || clock->iffCondition)
    return nullptr;
  const auto resolved = connected_signal(name(*clock_signal));
  if(resolved != clock_gate->clock && clock_gate_enable(resolved).empty())
    return nullptr;

  bool has_assignment = false;
  bool all_nonblocking = true;
  timed->stmt.visit(makeVisitor(
    [&](auto &visitor, const AssignmentExpression &assignment)
    {
      if(assignment.left().getSymbolReference() == &value)
      {
        has_assignment = true;
        all_nonblocking &=
          assignment.isNonBlocking() && !assignment.timingControl;
      }
      visitor.visitDefault(assignment);
    },
    [&](auto &visitor, const UnaryExpression &unary)
    {
      if(
        OpInfo::isLValue(unary.op) &&
        unary.operand().getSymbolReference() == &value)
        all_nonblocking = false;
      visitor.visitDefault(unary);
    }));
  return has_assignment && all_nonblocking ? process : nullptr;
}

std::optional<compilert::clock_gate_resett> compilert::clock_gate_reset_source(
  const async_resett &reset,
  bool allow_independent) const
{
  const auto &gate = *clock_gate;
  auto is_bit = [](const Type &type)
  { return type.isIntegral() && type.getBitWidth() == 1; };
  std::set<const ValueSymbol *> seen;
  const auto *signal = reset.signal;
  bool active_low = reset.active_low;
  while(is_bit(signal->getType()) && seen.insert(signal).second)
  {
    const auto id = name(*signal);
    if(id == gate.reset_n)
      return active_low ? std::optional{clock_gate_resett::External}
                        : std::nullopt;
    if(id == gate.reset)
      return active_low ? std::nullopt
                        : std::optional{clock_gate_resett::Synchronized};

    if(auto output = gate.output_sources.find(id);
       output != gate.output_sources.end())
    {
      if(!output->second || am.getDrivers(*signal).size() != 1)
        return {};
      signal = output->second;
      continue;
    }

    const Expression *source = clock_gate_value_source(*signal);
    if(!source || !is_bit(*source->type))
      return {};
    // Only a one-bit inverter is transparent to the timing proof. Its
    // output edge corresponds to the opposite edge at the actual source.
    while(const auto *negation = source->as_if<UnaryExpression>())
    {
      if(
        (negation->op != UnaryOperator::BitwiseNot &&
         negation->op != UnaryOperator::LogicalNot) ||
        !is_bit(*negation->operand().type))
        return {};
      active_low = !active_low;
      source = &negation->operand();
    }
    if(const auto *alias = named_value(source))
    {
      signal = alias;
      continue;
    }
    if(!active_low && allow_independent && certifies_clock_gate_reset(*signal))
      return clock_gate_resett::Independent;
    return {};
  }
  return {};
}

bool compilert::certifies_clock_gate_reset(const ValueSymbol &reset) const
{
  const auto &gate = *clock_gate;
  std::set<const ValueSymbol *> active;
  auto dereference =
    [&](auto &&self, const ValueSymbol &value) -> const Expression *
  {
    if(!active.insert(&value).second)
      return nullptr;
    const Expression *expr = nullptr;
    if(auto output = gate.output_sources.find(name(value));
       output != gate.output_sources.end())
    {
      if(output->second && am.getDrivers(value).size() == 1)
        expr = self(self, *output->second);
    }
    else if(const auto *source = clock_gate_value_source(value))
    {
      const auto *alias = named_value(source);
      expr = alias ? self(self, *alias) : source;
    }
    active.erase(&value);
    return expr;
  };
  const auto *mux = dereference(dereference, reset);
  const auto *select = mux ? mux->as_if<ConditionalExpression>() : nullptr;
  if(!select || select->conditions.size() != 1 || select->conditions[0].pattern)
    return false;
  const auto *te = named_value(&*select->conditions[0].expr);
  const auto *invert = select->left().as_if<UnaryExpression>();
  const auto *rst_n = invert ? named_value(&invert->operand()) : nullptr;
  if(
    !te || connected_signal(name(*te)) != gate.test || !rst_n ||
    invert->op != UnaryOperator::BitwiseNot ||
    connected_signal(name(*rst_n)) != gate.reset_n)
    return false;
  const auto *active_or = select->right().as_if<BinaryExpression>();
  if(!active_or || active_or->op != BinaryOperator::BinaryOr)
    return false;

  auto producer_reset_bit = [&](const Expression &term) -> std::optional<bool>
  {
    const auto *net = named_value(&term);
    const auto *expr = net ? dereference(dereference, *net) : &term;
    const auto *select =
      expr ? expr->as_if<ElementSelectExpression>() : nullptr;
    const auto *reg = select ? named_value(&select->value()) : nullptr;
    const auto *proc = reg ? clock_gate_register_driver(*reg) : nullptr;
    if(!proc || !reg->getType().isIntegral())
      return {};
    const auto &timed = proc->getBody().as<TimedStatement>();
    if(gated_clock_enable(first_event(timed)->expr) != gate.enable)
      return {};
    const auto async = match_async_reset(timed);
    if(
      !async ||
      clock_gate_reset_source(*async, false) != clock_gate_resett::Synchronized)
      return {};
    const auto *assign = reset_assignment(async->body->ifTrue, *reg);
    if(!assign)
      return {};
    unsigned normal_writes = 0;
    if(async->body->ifFalse)
      async->body->ifFalse->visit(makeVisitor(
        [&](auto &visitor, const AssignmentExpression &assignment)
        {
          if(assignment.left().getSymbolReference() == reg)
            ++normal_writes;
          visitor.visitDefault(assignment);
        }));
    if(normal_writes > 1)
      return {};
    EvalContext context(*proc);
    const auto selected = select->selector().eval(context);
    const auto constant = assign->right().eval(context);
    const auto index =
      selected.isInteger() ? selected.integer().as<int32_t>() : std::nullopt;
    const auto range = reg->getType().getFixedRange();
    if(
      !index || !range.containsPoint(*index) || !constant.isInteger() ||
      constant.integer().hasUnknown() ||
      constant.integer().getBitWidth() != reg->getType().getBitWidth())
      return {};
    return bool(constant.integer()[range.translateIndex(*index)]);
  };

  // Both inputs change only with an NBA on the RstSync parent or with its
  // proved asynchronous reset. At a boundary with both events, the shared
  // reset must leave the OR asserted so that a short assertion cannot be lost.
  const auto left = producer_reset_bit(active_or->left());
  const auto right = producer_reset_bit(active_or->right());
  return left && right && (*left || *right);
}

void compilert::verify_clock_gate_enable(const Expression &enable) const
{
  const auto &gate = *clock_gate;
  std::set<const ValueSymbol *> visiting, checked;
  std::function<void(const Expression &)> expression;
  std::function<void(const ValueSymbol &)> signal;
  signal = [&](const ValueSymbol &value)
  {
    if(
      checked.count(&value) || value.kind == SymbolKind::Parameter ||
      value.kind == SymbolKind::EnumValue)
      return;
    auto id = name(value);
    auto established = connected_signal(id);
    if(established == gate.test || established == gate.reset_n)
      return;
    if(established == gate.clock || !clock_gate_enable(established).empty())
      fail(
        value.location,
        "clock gate contract does not support clock data in enable");
    if(!visiting.insert(&value).second)
      fail(
        value.location,
        "clock gate contract found an enable computation cycle");
    if(auto output = gate.output_sources.find(id);
       output != gate.output_sources.end())
    {
      if(!output->second || am.getDrivers(value).size() != 1)
        fail(
          value.location,
          "clock gate contract cannot resolve a unique enable driver");
      signal(*output->second);
    }
    else if(const auto *source = clock_gate_value_source(value))
      expression(*source);
    else
    {
      const auto *process = clock_gate_register_driver(value);
      if(!process)
        fail(
          value.location,
          "clock gate contract cannot prove a sampled enable driver");
      const auto &timed = process->getBody().as<TimedStatement>();
      if(timed.timing.kind == TimingControlKind::EventList)
      {
        const auto reset = match_async_reset(timed);
        if(!reset || !clock_gate_reset_source(*reset, true))
          fail(
            value.location,
            "clock gate contract cannot prove enable reset timing");
      }
    }
    visiting.erase(&value);
    checked.insert(&value);
  };
  expression = [&](const Expression &expr)
  {
    require_read_only(
      expr, "clock gate contract requires a side-effect-free enable");
    expr.visit(makeVisitor(
      [&](auto &, const CallExpression &call)
      {
        fail(
          call.sourceRange.start(),
          "clock gate contract does not support a call in enable");
      }));
    expr.visitSymbolReferences(
      [&](const Expression &at, const Symbol &symbol)
      {
        if(const auto *value = symbol.as_if<ValueSymbol>())
          signal(*value);
        else
          fail(
            at.sourceRange.start(),
            "clock gate contract cannot prove an enable reference");
      });
  };
  expression(enable);
}

void compilert::load_clock_gate(
  const std::string &path,
  const InstanceSymbol &top)
{
  auto reject = [&](const std::string &why)
  { throw std::runtime_error(path + ": clock gate contract: " + why); };
  std::ifstream in(path);
  std::string version, env1, env2, sync_key, mux_key, test, reset_n, clock,
    sync_path, mux, extra;
  if(
    !(in >> version >> env1 >> test >> env2 >> reset_n >> clock >> sync_key >>
      sync_path >> mux_key >> mux) ||
    version != vv_objectt::clock_gate_v1 || env1 != "ENV1" || env2 != "ENV2" ||
    sync_key != "RstSync" || mux_key != "reset-mux")
    reject(
      "expected coral-w4-env1-env2.v1, ENV1, ENV2, RstSync and reset-mux "
      "bindings");

  std::map<std::string, const InstanceSymbol *> listed;
  while(in >> extra)
  {
    std::string instance;
    if(extra != "ClockGate" || !(in >> instance))
      reject("expected ClockGate and its elaborated instance path");
    if(!listed.emplace(instance, nullptr).second)
      reject("duplicate ClockGate instance " + instance);
  }

  auto prefix = std::string(root) + ".";
  clock_gatet gate;
  gate.clock = prefix + clock;
  gate.reset_n = prefix + reset_n;
  gate.test = prefix + test;
  gate.reset = prefix + mux;
  auto is_input = [&](const std::string &id)
  {
    for(const auto *p : top.body.getPortList())
      if(
        p->kind == SymbolKind::Port &&
        p->as<PortSymbol>().direction == ArgumentDirection::In &&
        p->as<PortSymbol>().internalSymbol &&
        name(*p->as<PortSymbol>().internalSymbol) == id &&
        p->as<PortSymbol>().getType().isIntegral() &&
        p->as<PortSymbol>().getType().getBitWidth() == 1)
        return true;
    return false;
  };
  if(
    !is_input(gate.clock) || !is_input(gate.reset_n) || !is_input(gate.test) ||
    gate.clock == gate.reset_n || gate.clock == gate.test ||
    gate.reset_n == gate.test)
    reject("ENV1 and ENV2 must bind three distinct scalar top inputs");

  std::vector<const InstanceSymbol *> instances;
  collect_instances(top.body, instances, gate.signals);
  for(const auto *instance : instances)
  {
    if(instance->getHierarchicalPath() == sync_path)
      gate.sync = instance;
    if(auto found = listed.find(instance->getHierarchicalPath());
       found != listed.end())
      found->second = instance;
    for(const auto *connection : instance->getPortConnections())
    {
      const auto *p = connection->port.as_if<PortSymbol>();
      const auto *value = port_expression(*connection);
      const auto *actual = named_value(value);
      const auto *internal = p && p->internalSymbol
                               ? p->internalSymbol->as_if<ValueSymbol>()
                               : nullptr;
      if(internal && value)
      {
        if(p->direction == ArgumentDirection::In)
          gate.input_values.emplace(name(*p->internalSymbol), value);
        else if(p->direction == ArgumentDirection::Out && actual)
        {
          auto [it, unique] =
            gate.output_sources.emplace(name(*actual), internal);
          if(!unique)
            it->second = nullptr;
        }
      }
      if(
        p && actual && p->internalSymbol &&
        (p->direction == ArgumentDirection::In ||
         p->direction == ArgumentDirection::Out) &&
        p->getType().isIntegral() && p->getType().getBitWidth() == 1 &&
        actual->getType().isIntegral() && actual->getType().getBitWidth() == 1)
        gate_connections.emplace(name(*p->internalSymbol), name(*actual));
    }
  }
  if(!gate.sync)
    reject("unbound RstSync instance " + sync_path);
  if(!has_active_definition(gate.sync->getDefinition(), rst_sync_v1))
    fail(
      gate.sync->location,
      "clock gate contract requires the Coral v1 RstSync body");
  const auto *icg = gate.sync->body.find("icg");
  if(
    !icg || icg->kind != SymbolKind::Instance ||
    !has_active_definition(
      icg->as<InstanceSymbol>().getDefinition(), clock_gate_v1))
    fail(
      gate.sync->location,
      "clock gate contract requires the Coral v1 low-phase ClockGate body");

  for(const auto *connection : gate.sync->getPortConnections())
  {
    const auto &p = connection->port.as<PortSymbol>();
    gate.ports.emplace(std::string(p.name), name(*p.internalSymbol));
    if(p.name == "clk_en")
    {
      const auto *value = port_expression(*connection);
      EvalContext context(*gate.sync);
      if(!value || !value->eval(context).isTrue())
        fail(p.location, "clock gate contract requires RstSync.clk_en = 1");
    }
  }
  auto port = [&](const char *key)
  { return connected_signal(gate.ports.at(key)); };
  if(
    port("clk_i") != gate.clock || port("rstn_i") != gate.reset_n ||
    port("te") != gate.test)
    fail(
      gate.sync->location,
      "clock gate contract ENV1/ENV2 port binding mismatch");

  auto driven_net = [&](const char *key)
  {
    auto id = port(key);
    const auto signal = gate.signals.find(id);
    if(
      id == gate.ports.at(key) || signal == gate.signals.end() ||
      signal->second->kind != SymbolKind::Net ||
      signal->second->getInitializer() ||
      am.getDrivers(*signal->second).size() != 1)
      fail(
        gate.sync->location,
        "clock gate contract requires a single driven RstSync output " +
          std::string(key));
    return id;
  };
  gate.output = driven_net("clk_o");
  const auto synchronized_reset_n = driven_net("rstn_o");
  gate.enable = name(*gate.sync) + ".__clock_gate_enable";
  const auto reset = gate.signals.find(gate.reset);
  if(reset == gate.signals.end())
    reject("unbound reset mux " + mux);
  const auto *init = reset->second->getInitializer();
  const auto *invert = init ? init->as_if<UnaryExpression>() : nullptr;
  const auto *select =
    invert ? invert->operand().as_if<ConditionalExpression>() : nullptr;
  auto is_bound = [&](const Expression *expr, const std::string &id)
  {
    const auto *signal = named_value(expr);
    return signal && connected_signal(name(*signal)) == id;
  };
  if(
    reset->second->kind != SymbolKind::Net || !invert ||
    invert->op != UnaryOperator::BitwiseNot || !select ||
    select->conditions.size() != 1 || select->conditions[0].pattern ||
    !is_bound(&*select->conditions[0].expr, gate.test) ||
    !is_bound(&select->left(), gate.reset_n) ||
    !is_bound(&select->right(), synchronized_reset_n) ||
    am.getDrivers(*reset->second).size() > 1)
    fail(
      reset->second->location,
      "clock gate contract ENV1 reset mux binding mismatch");

  auto &metadata = object.metadata.add("clock_gate_contract");
  metadata.id(vv_objectt::clock_gate_v1);
  metadata.set("ENV1", gate.test);
  metadata.set("ENV2", gate.reset_n);
  metadata.set("ENV2_due", gate.reset_n + "$ENV2_due");
  metadata.set("master", gate.clock);
  metadata.set("gate", gate.output);
  metadata.set("reset_mux", gate.reset);
  metadata.set("RstSync", sync_path);
  metadata.set("sampling", vv_objectt::clock_gate_sampling);
  metadata.add(ID_C_source_location) = loc(gate.sync->location);
  for(const auto &[path, instance] : listed)
  {
    if(!instance)
      reject("unbound ClockGate instance " + path);
    if(
      instance == &icg->as<InstanceSymbol>() ||
      !has_active_definition(instance->getDefinition(), clock_gate_v1))
      fail(
        instance->location,
        "clock gate contract requires an independent Coral v1 low-phase "
        "ClockGate body");
    auto binding = [&](std::string_view key) -> const ValueSymbol &
    {
      const auto *symbol = instance->body.find(key);
      if(!symbol || !symbol->as_if<ValueSymbol>())
        fail(
          instance->location,
          "clock gate contract requires port " + std::string(key));
      return symbol->as<ValueSymbol>();
    };
    auto input = connected_signal(name(binding("clk_i")));
    auto output = connected_signal(name(binding("clk_o")));
    const auto sink = gate.signals.find(output);
    if(connected_signal(name(binding("te"))) != gate.test)
      fail(
        instance->location, "clock gate contract ClockGate.te must bind ENV1");
    if(
      output == name(binding("clk_o")) || output == gate.output ||
      output == gate.clock || output == gate.reset || output == gate.reset_n ||
      output == gate.test || sink == gate.signals.end() ||
      sink->second->kind != SymbolKind::Net || sink->second->getInitializer() ||
      am.getDrivers(*sink->second).size() != 1)
      fail(
        instance->location,
        "clock gate contract requires a single driven ClockGate output");
    auto enable = name(*instance) + ".__clock_gate_enable";
    if(!gate.branches
          .emplace(output, clock_gate_branch_t{instance, input, output, enable})
          .second)
      fail(
        instance->location,
        "clock gate contract ClockGate output is already bound");
  }
  for(const auto &[output, branch] : gate.branches)
  {
    std::set<std::string> visited{output};
    auto parent = branch.input;
    while(parent != gate.clock && parent != gate.output)
    {
      auto found = gate.branches.find(parent);
      if(found == gate.branches.end() || !visited.insert(parent).second)
        fail(
          branch.instance->location,
          "clock gate contract ClockGate input is outside the root domain or "
          "forms a cycle");
      parent = found->second.input;
    }
    auto &entry =
      metadata.add("ClockGate").add(branch.instance->getHierarchicalPath());
    entry.id("low-phase-preponed");
    entry.set("input", branch.input);
    entry.set("output", output);
    entry.set("enable", name(*branch.instance->body.find("enable")));
    entry.set("effective_enable", branch.enable);
    entry.add(ID_C_source_location) = loc(branch.instance->location);
  }
  object.metadata.set("clock", gate.clock);
  clock_gate = std::move(gate);
  for(const auto &[output, branch] : clock_gate->branches)
  {
    auto input = clock_gate->input_values.find(
      name(*branch.instance->body.find("enable")));
    if(input == clock_gate->input_values.end())
      fail(
        branch.instance->location,
        "clock gate contract requires a connected ClockGate.enable");
    verify_clock_gate_enable(*input->second);
  }
}

void compilert::emit_clock_gate(exprt &items)
{
  const auto &gate = *clock_gate;
  auto port = [&](const char *name)
  { return object.symbols.lookup_ref(gate.ports.at(name)).symbol_expr(); };
  auto where = loc(gate.sync->location);
  auto width = unsignedbv_typet{4};
  auto shift =
    add_local(object, name(*gate.sync) + ".__clock_gate_shift", width, where);
  auto enable = add_local(object, gate.enable, bool_typet(), where);
  auto clear = from_integer(0, width);
  auto rst_n = port("rstn_i");
  auto effective = if_exprt{rst_n, shift, clear};
  auto advanced = node(
    ID_bitor,
    width,
    {node(ID_shl, width, {effective, from_integer(1, width)}),
     from_integer(1, width)});
  items.operands().push_back(
    assign(port("rstn_o"), extractbit_exprt{effective, 1}));
  items.operands().push_back(assign(enable, extractbit_exprt{effective, 3}));
  items.operands().push_back(
    assign(port("clk_o"), and_exprt{port("clk_i"), enable}));
  items.operands().push_back(item(
    ID_verilog_always,
    {root_edge(gate.clock, update(shift, if_exprt{rst_n, advanced, clear}))}));
}

void compilert::emit_clock_gate_branch(
  const clock_gate_branch_t &branch,
  exprt &items)
{
  auto port = [&](std::string_view key)
  {
    return object.symbols.lookup_ref(name(*branch.instance->body.find(key)))
      .symbol_expr();
  };
  auto effective = add_local(
    object, branch.enable, bool_typet(), loc(branch.instance->location));
  auto local = port("enable");
  auto parent = clock_gate_enable(branch.input);
  items.operands().push_back(assign(
    effective,
    parent.empty()
      ? exprt{local}
      : exprt{and_exprt{symbol_exprt{parent, bool_typet()}, local}}));
  // Each parent output is low during the root low phase. A subsequent
  // root rising edge passes only when every latch sampled an open enable.
  items.operands().push_back(
    assign(port("clk_o"), and_exprt{port("clk_i"), local}));
}

void compilert::emit_clock_environment(exprt &items)
{
  const auto &gate = *clock_gate;
  const auto where = loc(gate.sync->location);
  auto rst_n = symbol_exprt{gate.reset_n, bool_typet()};
  auto prev =
    add_local(object, gate.reset_n + "$ENV2_previous", bool_typet(), where);
  auto due = add_local(object, gate.reset_n + "$ENV2_due", bool_typet(), where);
  items.operands().push_back(item(
    ID_initial,
    {item(
      ID_block,
      {item(ID_verilog_blocking_assign, {prev, true_exprt{}}),
       item(ID_verilog_blocking_assign, {due, false_exprt{}})})}));
  items.operands().push_back(item(
    ID_verilog_always,
    {root_edge(
      gate.clock,
      item(
        ID_block,
        {update(prev, rst_n),
         update(due, and_exprt{prev, not_exprt{rst_n}})}))}));
  add_environment_assume(
    object,
    items,
    "clock_gate_ENV1",
    gate.clock,
    not_exprt{symbol_exprt{gate.test, bool_typet()}},
    where);
  add_environment_assume(
    object,
    items,
    "clock_gate_ENV2",
    gate.clock,
    implies_exprt{due, not_exprt{rst_n}},
    where);
}

std::optional<exprt> compilert::gated_statement(
  const ProceduralBlockSymbol &process,
  const TimedStatement &timed,
  exprt &items)
{
  if(!clock_gate)
    return {};
  const auto &gate = *clock_gate;
  const auto *list = timed.timing.as_if<EventListControl>();
  const auto *edge = first_event(timed);
  auto enable_id = edge ? gated_clock_enable(edge->expr) : std::string{};
  if(enable_id.empty())
    return {};
  auto at = timed.timing.sourceRange.start();
  if(
    edge->edge != EdgeKind::PosEdge || edge->iffCondition ||
    (process.procedureKind != ProceduralBlockKind::Always &&
     process.procedureKind != ProceduralBlockKind::AlwaysFF))
    fail(at, "clock gate contract requires a positive-edge clocked process");
  auto enable = symbol_exprt{enable_id, bool_typet()};
  if(!list)
    return root_edge(gate.clock, item(ID_if, {enable, stmt(timed.stmt)}));
  if(list->events.size() != 2)
    fail(at, "clock gate contract requires one asynchronous reset");
  const auto reset = match_async_reset(timed);
  if(!reset)
    fail(at, "clock gate contract requires a reset edge matching the outer if");
  const auto reset_source =
    clock_gate_reset_source(*reset, enable_id != gate.enable);
  if(!reset_source)
    fail(
      at,
      "clock gate contract cannot prove the independent asynchronous reset "
      "timing");
  if(reset_source == clock_gate_resett::Independent)
    object.metadata.add("clock_gate_contract")
      .add("independent_resets")
      .set(
        connected_signal(name(*reset->signal)),
        vv_objectt::clock_gate_independent_reset_sampling);
  const auto *condition = reset->body;
  auto reset_test = boolean(expr(*condition->conditions[0].expr));
  auto reset_body = stmt(condition->ifTrue);
  auto normal = condition->ifFalse ? stmt(*condition->ifFalse) : item(ID_skip);

  // A constant reset action can be visible before the next root observation
  // even if no gated clock event occurs. Store the clocked value separately.
  std::map<irep_idt, symbol_exprt> storage;
  auto rewrite_reset = [&](auto &&self, exprt &statement) -> void
  {
    if(statement.id() == ID_block)
    {
      for(auto &child : statement.operands())
        self(self, child);
      return;
    }
    if(
      statement.id() != ID_verilog_non_blocking_assign ||
      statement.operands().size() != 2 ||
      statement.operands().front().id() != ID_symbol)
      fail(
        at,
        "clock gate contract requires full constant nonblocking reset "
        "assignments");
    auto &assignment = to_verilog_non_blocking_assign(statement);
    auto original = to_symbol_expr(assignment.lhs());
    auto value = simplify_expr(assignment.rhs(), namespacet{object.symbols});
    if(
      !value.is_constant() || value.type() != original.type() ||
      (value.type().id() != ID_bool && value.type().id() != ID_unsignedbv &&
       value.type().id() != ID_signedbv))
      fail(at, "clock gate contract requires a known integral reset value");
    assignment.rhs() = std::move(value);
    const auto signal = gate.signals.find(id2string(original.identifier()));
    if(signal == gate.signals.end() || signal->second->getInitializer())
      fail(at, "clock gate contract requires an uninitialized reset target");
    for(const auto *driver : am.getDrivers(*signal->second))
      if(&*driver->containingSymbol != &process)
        fail(at, "clock gate contract requires a uniquely driven reset target");
    auto raw = add_local(
      object,
      id2string(original.identifier()) + "$clock_gate_raw",
      original.type(),
      loc(at));
    if(!storage.emplace(original.identifier(), raw).second)
      fail(at, "clock gate contract reset assigns a target more than once");
    items.operands().push_back(
      assign(original, if_exprt{reset_test, assignment.rhs(), raw}));
    assignment.lhs() = raw;
  };
  rewrite_reset(rewrite_reset, reset_body);
  auto rewrite_normal = [&](auto &&self, exprt &statement) -> void
  {
    if(statement.id() == ID_skip)
      return;
    if(statement.id() == ID_block)
    {
      for(auto &child : statement.operands())
        self(self, child);
      return;
    }
    if(statement.id() == ID_if)
    {
      for(std::size_t i = 1; i < statement.operands().size(); ++i)
        self(self, statement.operands()[i]);
      return;
    }
    if(
      statement.id() != ID_verilog_non_blocking_assign ||
      statement.operands().size() != 2 ||
      statement.operands().front().id() != ID_symbol)
      fail(at, "clock gate contract requires full nonblocking updates");
    auto &assignment = to_verilog_non_blocking_assign(statement);
    auto found = storage.find(to_symbol_expr(assignment.lhs()).identifier());
    if(found == storage.end())
      fail(at, "clock gate contract update target lacks a constant reset");
    assignment.lhs() = found->second;
  };
  rewrite_normal(rewrite_normal, normal);
  return root_edge(
    gate.clock,
    item(
      ID_if,
      {std::move(reset_test),
       std::move(reset_body),
       item(ID_if, {enable, std::move(normal)})}));
}
} // namespace vv
