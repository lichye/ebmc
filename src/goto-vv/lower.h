#pragma once
#include <util/arith_tools.h>
#include <util/mathematical_types.h>
#include <util/std_expr.h>

#include <slang/analysis/AnalysisManager.h>
#include <slang/analysis/AnalyzedAssertion.h>
#include <slang/analysis/ValueDriver.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/EvalContext.h>
#include <slang/ast/expressions/AssertionExpr.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/CallExpression.h>
#include <slang/ast/expressions/ConversionExpression.h>
#include <slang/ast/expressions/LiteralExpressions.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/expressions/SelectExpressions.h>
#include <slang/ast/statements/ConditionalStatements.h>
#include <slang/ast/statements/LoopStatements.h>
#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/symbols/AttributeSymbol.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/ParameterSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/driver/Driver.h>
#include <slang/syntax/AllSyntax.h>
#include <verilog/verilog_expr.h>
#include <verilog/verilog_types.h>
#include <verilog/vv_object.h>

#include <map>
#include <mutex>
#include <stdexcept>

namespace vv
{
using namespace slang;
using namespace slang::ast;
constexpr auto root = "Verilog::$root";
constexpr auto sampling = "single-posedge/preponed/disable-current";
inline std::string name(const Symbol &s)
{
  return std::string(root) + "." + s.getHierarchicalPath();
}
inline exprt node(irep_idt id, const typet &t, exprt::operandst ops = {})
{
  exprt r(id, t);
  r.operands() = std::move(ops);
  return r;
}
inline exprt item(irep_idt id, exprt::operandst ops = {})
{
  return node(id, typet(ID_nil), std::move(ops));
}
inline exprt boolean(exprt e)
{
  return typecast_exprt::conditional_cast(e, bool_typet());
}

inline bool has_packed_elements(const PackedArrayType &array)
{
  return array.elementType.getBitWidth() != 1 ||
         array.elementType.getCanonicalType().kind ==
           SymbolKind::PackedArrayType;
}

inline bool needs_packed_element_layout(const PackedArrayType &array)
{
  return has_packed_elements(array) || !array.range.isDescending() ||
         array.range.right != 0;
}

class compilert
{
public:
  SourceManager &sm;
  analysis::AnalysisManager &am;
  vv_objectt object;
  const Symbol *scope = nullptr;
  bool in_clocked_property = false;
  bool in_always_comb = false;
  std::map<const ConcurrentAssertionStatement *, const TimingControl *> clocks;
  std::map<std::string, std::string> clock_alias;
  std::map<const Symbol *, exprt> locals;
  struct procedural_localt
  {
    symbol_exprt ref;
    bool assigned = false;
    bool require_full_assignment = false;
    ConstantValue constant;
  };
  std::map<const Symbol *, procedural_localt> procedural_locals;
  std::map<const Symbol *, ConstantValue> expanded_indices;
  std::size_t next_procedural_local = 0;
  explicit compilert(SourceManager &sm, analysis::AnalysisManager &am)
    : sm(sm), am(am)
  {
  }
  source_locationt loc(SourceLocation l) const;
  [[noreturn]] void fail(SourceLocation l, const std::string &why) const;
  irept describe_type(const Type &type) const;
  typet type(const Type &t, SourceLocation fallback = {}) const;
  exprt literal(const SVInt &v, const typet &t) const;
  ConstantValue eval_known(const Expression &e) const;
  exprt expr(
    const Expression &e,
    const exprt *compound_target = nullptr,
    bool lvalue = false);
  exprt lhs(const Expression &e)
  {
    return expr(e, nullptr, true);
  }
  std::optional<exprt>
  dynamic_packed_write(const AssignmentExpression &assignment);
  void require_empty_final(const Statement &s) const;
  exprt stmt(const Statement &s);
  exprt counted_loop(
    const VariableSymbol &index,
    const ForLoopStatement &loop,
    const AssignmentExpression *external_init = nullptr);
  exprt declare_local(const VariableSymbol &variable);
  exprt clock_reset_block(const TimedStatement &block);
  exprt immediate(const ImmediateAssertionStatement &s, const Symbol *label);
  exprt assertion(const AssertionExpr &a, SourceLocation at);
  std::string clock(const TimingControl *ev, SourceLocation at);
  void module(const InstanceSymbol &inst, bool top);
  using port_mapt = std::map<const Symbol *, const PortSymbol *>;
  void members(const Scope &body, const port_mapt &ports, exprt &items);
  void property(
    const ConcurrentAssertionStatement &s,
    const Symbol &label,
    exprt &items);
  void declare(const ValueSymbol &s, const PortSymbol *p, exprt &items);
  symbolt &add(const Symbol &s, const typet &t);
};
} // namespace vv
