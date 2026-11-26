#include "lower.h"

namespace vv
{
namespace
{
bool scalar(const Type &type)
{
  return type.isIntegral() && type.getBitWidth() == 1;
}

const ValueSymbol *scalar_port(const PortSymbol &port)
{
  const auto *value = port.internalSymbol
                        ? port.internalSymbol->as_if<ValueSymbol>()
                        : nullptr;
  if(
    !value || !scalar(port.getType()) || !scalar(value->getType()) ||
    port.getType().isFourState() != value->getType().isFourState())
    return nullptr;
  const auto *internal = port.getInternalExpr();
  if(internal && scalar_clock_signal(internal) != value)
    return nullptr;
  return value;
}

const Scope *driver_scope(const Scope &scope, const analysis::AnalysisManager &am)
{
  if(am.getAnalyzedScope(scope))
    return &scope;

  const auto &symbol = scope.asSymbol();
  if(const auto *body = symbol.as_if<InstanceBodySymbol>())
  {
    const auto *instance = body->parentInstance;
    if(const auto *canonical = instance->getCanonicalBody())
      return canonical;
    const auto *parent = instance->getParentScope();
    const auto *checked = parent ? driver_scope(*parent, am) : nullptr;
    if(!checked || checked == parent)
      return &scope;
    const auto *same = checked->find(instance->name);
    const auto *other = same ? same->as_if<InstanceSymbol>() : nullptr;
    return other ? driver_scope(other->body, am) : nullptr;
  }
  if(!symbol.as_if<GenerateBlockSymbol>() &&
     !symbol.as_if<GenerateBlockArraySymbol>())
    return &scope;

  const auto *parent = symbol.getParentScope();
  const auto *checked = parent ? driver_scope(*parent, am) : nullptr;
  if(!checked || checked == parent)
    return checked ? &scope : nullptr;

  if(const auto *block = symbol.as_if<GenerateBlockSymbol>())
  {
    if(const auto *index = block->getArrayIndex())
    {
      const auto *array = checked->asSymbol().as_if<GenerateBlockArraySymbol>();
      if(array)
        for(const auto *entry : array->entries)
          if(entry->getArrayIndex() && *entry->getArrayIndex() == *index)
            return entry;
      return nullptr;
    }
    const auto *same = checked->find(block->getExternalName());
    return same ? same->as_if<GenerateBlockSymbol>() : nullptr;
  }
  const auto &array = symbol.as<GenerateBlockArraySymbol>();
  const auto *same = checked->find(array.getExternalName());
  return same ? same->as_if<GenerateBlockArraySymbol>() : nullptr;
}
} // namespace

const Expression *port_expression(const PortConnection &connection)
{
  auto *expression = connection.getExpression();
  if(const auto *assign =
       expression ? expression->as_if<AssignmentExpression>() : nullptr;
     assign && assign->isLValueArg())
    expression = &assign->left();
  return expression;
}

const ValueSymbol *scalar_clock_signal(const Expression *expression)
{
  if(
    !expression ||
    (expression->kind != ExpressionKind::NamedValue &&
     expression->kind != ExpressionKind::HierarchicalValue) ||
    !scalar(*expression->type))
    return nullptr;
  const auto *symbol = expression->getSymbolReference();
  if(
    !symbol ||
    (symbol->kind != SymbolKind::Net && symbol->kind != SymbolKind::Variable))
    return nullptr;
  return &symbol->as<ValueSymbol>();
}

void compilert::index_clock_links(const InstanceSymbol &top)
{
  auto link = [&](
                const ValueSymbol &to,
                const ValueSymbol &from,
                const Symbol &driver)
  {
    // No implicit state-domain conversion in a pass-through.
    if(to.getType().isFourState() != from.getType().isFourState())
      return;
    auto [it, unique] = clock_links.emplace(&to, clock_linkt{&from, &driver});
    if(!unique)
      it->second = {nullptr, nullptr};
  };

  for(const auto *port : top.body.getPortList())
    if(const auto *p = port->as_if<PortSymbol>();
       p && p->direction == ArgumentDirection::In)
      if(const auto *value = scalar_port(*p);
         value && !value->getType().isSigned())
        clock_inputs.insert(value);

  auto visit = [&](auto &&self, const Scope &body) -> void
  {
    for(const auto &member : body.members())
    {
      if(const auto *inst = member.as_if<InstanceSymbol>())
      {
        for(const auto *connection : inst->getPortConnections())
        {
          const auto *port = connection->port.as_if<PortSymbol>();
          const auto *formal = port ? scalar_port(*port) : nullptr;
          const auto *actual = scalar_clock_signal(port_expression(*connection));
          if(!formal || !actual)
            continue;
          if(port->direction == ArgumentDirection::In)
            link(*formal, *actual, inst->body);
          else if(port->direction == ArgumentDirection::Out)
            link(*actual, *formal, *inst);
        }
        self(self, inst->body);
      }
      else if(const auto *block = member.as_if<GenerateBlockSymbol>())
      {
        if(!block->isUninstantiated)
          self(self, *block);
      }
      else if(const auto *array = member.as_if<GenerateBlockArraySymbol>())
      {
        for(const auto *entry : array->entries)
          if(!entry->isUninstantiated)
            self(self, *entry);
      }
      else if(const auto *assign = member.as_if<ContinuousAssignSymbol>())
      {
        const auto *a = assign->getAssignment().as_if<AssignmentExpression>();
        if(!a || a->timingControl || assign->getDelay())
          continue;
        const auto strengths = assign->getDriveStrength();
        if(strengths.first || strengths.second)
          continue;
        const auto *target = scalar_clock_signal(&a->left());
        const auto *source = scalar_clock_signal(&a->right());
        if(target && source)
          link(*target, *source, *assign);
      }
    }
  };
  visit(visit, top.body);
}

const ValueSymbol *compilert::clock_source(
  const ValueSymbol &signal,
  SourceLocation diagnose) const
{
  const auto reject = [&](const ValueSymbol &at, const char *reason)
    -> const ValueSymbol *
  {
    if(diagnose)
      fail(diagnose, std::string(reason) + " " + at.getHierarchicalPath());
    return nullptr;
  };

  std::set<const ValueSymbol *> seen;
  for(auto *value = &signal;;)
  {
    if(!seen.insert(value).second)
      return reject(*value, "cycle in clock connections at");
    if(clock_gate && !clock_gate_enable(name(*value)).empty())
      return value;

    const bool primary = clock_inputs.count(value);
    const auto found = clock_links.find(value);
    if(!primary && found == clock_links.end() && !diagnose)
      return nullptr;
    const auto drivers = am.getDrivers(*value);
    if(
      drivers.size() > 1 ||
      (found != clock_links.end() && !found->second.source))
      return reject(*value, "multiple drivers for clock signal");
    if(!primary && found == clock_links.end())
    {
      if(!object.metadata.get("clock").empty())
        fail(diagnose, "multiple clock domains");
      return reject(*value, "unsupported clock source");
    }
    if(value->getInitializer())
      return reject(*value, "unsupported clock source");

    // Connections belong to the actual instance. Slang may analyze its
    // identical canonical body instead, so check that body for extra drivers.
    const auto *parent = value->getParentScope();
    const auto *canonical = parent ? driver_scope(*parent, am) : nullptr;
    if(!canonical)
      return reject(*value, "unsupported clock source");
    auto checked = drivers;
    if(canonical != parent)
    {
      const auto *same = canonical->find(value->name);
      if(!same || !same->as_if<ValueSymbol>())
        return reject(*value, "unsupported clock source");
      checked = am.getDrivers(same->as<ValueSymbol>());
      if(checked.size() > 1)
        return reject(*value, "multiple drivers for clock signal");
    }
    const auto *expected = primary ? nullptr : found->second.driver;
    auto matches = [&](const analysis::ValueDriver &driver)
    {
      if(primary || expected->kind == SymbolKind::InstanceBody)
        return driver.isInputPort();
      if(expected->kind == SymbolKind::Instance)
        return driver.flags.has(analysis::DriverFlags::OutputPort) &&
               driver.containingSymbol->location == expected->location;
      return driver.containingSymbol == expected ||
             (driver.containingSymbol->kind == SymbolKind::ContinuousAssign &&
              driver.containingSymbol->location == expected->location);
    };
    if(
      checked.size() != 1 || !matches(*checked.front()) ||
      (!drivers.empty() && !matches(*drivers.front())))
      return reject(*value, "unsupported clock source");
    if(primary)
      return value;
    value = found->second.source;
  }
}

exprt compilert::clock_edge(const Expression &event, const std::string &clock)
{
  auto original = expr(event);
  if(original.get(ID_identifier) == clock)
    return node(ID_posedge, bool_typet(), {std::move(original)});

  // The transition system samples the verified root. Retain the source
  // spelling and location on the edge, and retain the actual RTL connection.
  auto edge = node(
    ID_posedge,
    bool_typet(),
    {symbol_exprt{clock, bool_typet()}.with_source_location(
      original.source_location())});
  edge.add("source_clock") = std::move(original);
  return edge;
}
} // namespace vv
