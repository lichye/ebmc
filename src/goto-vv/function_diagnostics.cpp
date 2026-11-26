#include <slang/ast/ASTVisitor.h>

#include "lower.h"

namespace vv
{
namespace
{
bool package_task(std::string_view task)
{
  return task == "$fatal" || task == "$error";
}
} // namespace

bool compilert::checked_function(const SubroutineSymbol &callee) const
{
  if(!assert_errors && !assert_fatals)
    return false;
  if(auto known = checked_functions.find(&callee);
     known != checked_functions.end())
    return known->second;

  std::set<const SubroutineSymbol *> seen;
  auto scan = [&](auto &&self, const SubroutineSymbol &subroutine) -> bool
  {
    if(!seen.insert(&subroutine).second)
      return false;
    if(auto known = checked_functions.find(&subroutine);
       known != checked_functions.end())
      return known->second;

    const auto *parent = subroutine.getParentScope();
    const bool package = parent && parent->asSymbol().kind == SymbolKind::Package;
    bool selected = false;
    subroutine.getBody().visit(makeVisitor(
      [&](auto &visitor, const CallExpression &call)
      {
        if(selected)
          return;
        if(call.isSystemCall())
        {
          const auto task = call.getSubroutineName();
          selected = package && package_task(task) && assert_diagnostic(task);
        }
        else
          selected =
            self(self, *std::get<const SubroutineSymbol *>(call.subroutine));
        if(!selected)
          visitor.visitDefault(call);
      }));
    return selected;
  };
  const bool selected = scan(scan, callee);
  checked_functions.emplace(&callee, selected);
  return selected;
}

bool compilert::has_checked_call(const Expression &expression) const
{
  if(!assert_errors && !assert_fatals)
    return false;
  bool checked = false;
  expression.visit(makeVisitor(
    [&](auto &visitor, const CallExpression &call)
    {
      if(checked)
        return;
      if(!call.isSystemCall())
        checked =
          checked_function(*std::get<const SubroutineSymbol *>(call.subroutine));
      if(!checked)
        visitor.visitDefault(call);
    }));
  return checked;
}

void compilert::reject_checked_call(const Expression &expression) const
{
  if(has_checked_call(expression))
    fail(
      expression.sourceRange.start(),
      "cannot preserve evaluation path for a runtime diagnostic call");
}

exprt compilert::evaluated_expr(const Expression &expression)
{
  if(!assert_errors && !assert_fatals)
    return expr(expression);

  // These callers are expanded once with the current procedural guard.
  // Conversions do not re-evaluate their operand. Do not promote calls
  // under a conditional expression, an index, or a sampled operator.
  const auto *direct = &expression;
  while(const auto *conversion = direct->as_if<ConversionExpression>())
    direct = &conversion->operand();
  const auto *saved = std::exchange(evaluated_call, direct);
  auto value = expr(expression);
  evaluated_call = saved;
  return value;
}

exprt compilert::function_diagnostic_statement(const CallExpression &call)
{
  const auto task = call.getSubroutineName();
  const auto at = call.sourceRange.start();
  if(!call.isSystemCall() || !package_task(task))
    fail(at, "unsupported call statement in a pure runtime package function");
  for(const auto *argument : call.arguments())
  {
    check_diagnostic_argument(*argument);
    argument->visitSymbolReferences(
      [&](const Expression &reference, const Symbol &symbol)
      {
        if(
          VariableSymbol::isKind(symbol.kind) &&
          !is_function_variable(&symbol))
          fail(
            reference.sourceRange.start(),
            "runtime package diagnostic reads a nonlocal variable");
      });
  }
  function_has_diagnostic = true;
  if(!assert_diagnostic(task))
    return item(ID_skip).with_source_location(loc(at));
  if(!diagnostic_call || !locals.empty() || !expanded_indices.empty())
    fail(at, "cannot preserve evaluation path for a runtime diagnostic call");

  const auto entry = sm.getFullyExpandedLoc(diagnostic_call->entry);
  const auto where = loc(entry);
  const auto base = std::string(task) + "_call_" + id2string(where.get_line()) +
                    "_" + id2string(where.get_column()) + "_" +
                    std::to_string(next_diagnostic_call++);
  const auto id = diagnostic_call->instance + "." + base;
  auto check = procedural_assertion(entry, base, id, false_exprt{}, task);
  // A statically unexecuted call or body branch may be removed during RTL
  // construction. Its check is then vacuously true. A reached check gets
  // replaced by the assertion with the caller and callee guards.
  object.symbols.get_writeable_ref(id).value = true_exprt{};
  auto &property = object.metadata.add("properties").add(id);
  property.set("origin", "function-diagnostic-task");
  property.set("function", function_identifier);
  property.add("function_location") = loc(at);
  property.add("function_definition") = loc(function->location);
  property.add("inner_call_location") = loc(diagnostic_call->call);
  return check;
}

bool compilert::supplies_function_result(
  const exprt &statement,
  const irep_idt &result)
{
  const auto &operands = statement.operands();
  if(statement.id() == ID_return)
    return true;
  if(statement.id() == ID_verilog_blocking_assign)
    return operands[0].id() == ID_symbol &&
           operands[0].get(ID_identifier) == result;
  if(statement.id() == ID_block)
    return std::any_of(
      operands.begin(),
      operands.end(),
      [&](const exprt &child) { return supplies_function_result(child, result); });
  if(statement.id() == ID_if)
    return operands.size() == 3 && supplies_function_result(operands[1], result) &&
           supplies_function_result(operands[2], result);
  if(statement.id() == ID_verilog_case || statement.id() == ID_verilog_casez)
  {
    bool has_default = false;
    for(std::size_t i = 1; i < operands.size(); ++i)
    {
      const auto &arm = to_verilog_case_item(operands[i]);
      has_default |= arm.is_default();
      if(!supplies_function_result(arm.case_statement(), result))
        return false;
    }
    return has_default;
  }
  return false;
}
} // namespace vv
