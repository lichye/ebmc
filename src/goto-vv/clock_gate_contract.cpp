#include <util/bitvector_expr.h>
#include <util/namespace.h>
#include <util/simplify_expr.h>

#include <slang/syntax/SyntaxTree.h>

#include "lower.h"

#include <fstream>
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
  if(
    const auto *assign =
      expression ? expression->as_if<AssignmentExpression>() : nullptr)
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

bool compilert::gated_clock(const Expression &expression) const
{
  return clock_gate &&
         (expression.kind == ExpressionKind::NamedValue ||
          expression.kind == ExpressionKind::HierarchicalValue) &&
         expression.getSymbolReference() &&
         connected_signal(name(*expression.getSymbolReference())) ==
           clock_gate->output;
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
    (in >> extra) || version != vv_objectt::clock_gate_v1 || env1 != "ENV1" ||
    env2 != "ENV2" || sync_key != "RstSync" || mux_key != "reset-mux")
    reject(
      "expected coral-w4-env1-env2.v1, ENV1, ENV2, RstSync and reset-mux "
      "bindings");

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
    for(const auto *connection : instance->getPortConnections())
    {
      const auto *p = connection->port.as_if<PortSymbol>();
      const auto *actual = named_value(port_expression(*connection));
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
  object.metadata.set("clock", gate.clock);
  clock_gate = std::move(gate);
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
  const auto *edge = list && !list->events.empty()
                       ? list->events.front()->as_if<SignalEventControl>()
                       : timed.timing.as_if<SignalEventControl>();
  if(!edge || !gated_clock(edge->expr))
    return {};
  auto at = timed.timing.sourceRange.start();
  if(
    edge->edge != EdgeKind::PosEdge || edge->iffCondition ||
    (process.procedureKind != ProceduralBlockKind::Always &&
     process.procedureKind != ProceduralBlockKind::AlwaysFF))
    fail(at, "clock gate contract requires a positive-edge clocked process");
  auto enable = symbol_exprt{gate.enable, bool_typet()};
  if(!list)
    return root_edge(gate.clock, item(ID_if, {enable, stmt(timed.stmt)}));
  if(list->events.size() != 2)
    fail(at, "clock gate contract requires one asynchronous reset");
  const auto *reset = list->events[1]->as_if<SignalEventControl>();
  const auto *reset_signal = reset ? named_value(&reset->expr) : nullptr;
  const Statement *body = &timed.stmt;
  while(const auto *block = body->as_if<BlockStatement>())
    body = &block->body;
  const auto *condition = body->as_if<ConditionalStatement>();
  if(
    !reset || reset->edge != EdgeKind::PosEdge || reset->iffCondition ||
    !reset_signal || connected_signal(name(*reset_signal)) != gate.reset ||
    !condition || condition->conditions.size() != 1 ||
    condition->conditions[0].pattern ||
    named_value(&*condition->conditions[0].expr) != reset_signal)
    fail(
      at,
      "clock gate contract requires the bound asynchronous reset in the outer "
      "if");
  auto reset_test = boolean(expr(reset->expr));
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
