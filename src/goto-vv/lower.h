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
#include <slang/ast/symbols/SubroutineSymbols.h>
#include <slang/ast/symbols/VariableSymbols.h>
#include <slang/ast/types/AllTypes.h>
#include <slang/driver/Driver.h>
#include <slang/syntax/AllSyntax.h>
#include <verilog/verilog_expr.h>
#include <verilog/verilog_types.h>
#include <verilog/vv_object.h>

#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace vv
{
using namespace slang;
using namespace slang::ast;
constexpr auto root = "Verilog::$root";
constexpr auto sampling = "single-posedge/preponed/disable-current";
struct async_resett
{
  const SignalEventControl *event;
  const ValueSymbol *signal;
  const ConditionalStatement *body;
  bool active_low;
};

std::optional<async_resett> match_async_reset(const TimedStatement &);

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
  std::map<irep_idt, irept> signal_metadata;
  const Symbol *scope = nullptr;
  const SubroutineSymbol *function = nullptr;
  std::set<const SubroutineSymbol *> active_functions;
  std::string function_identifier;
  std::map<const VariableSymbol *, ConstantValue> function_constants;
  bool assert_warnings = false;
  bool assert_errors = false;
  bool assert_case_qualifiers = false;
  bool in_clocked_property = false;
  bool in_always_comb = false;
  bool in_posedge_process = false;
  bool in_gated_async = false;
  std::map<const ConcurrentAssertionStatement *, const TimingControl *> clocks;
  std::map<std::string, std::string> clock_alias;
  struct clock_gate_branch_t
  {
    const InstanceSymbol *instance;
    std::string input, output, enable;
  };
  struct clock_gatet
  {
    const InstanceSymbol *sync = nullptr;
    std::map<std::string, std::string> ports;
    std::map<std::string, const ValueSymbol *> signals;
    std::string clock, reset_n, test, output, reset;
    std::string enable;
    std::map<std::string, clock_gate_branch_t> branches;
    std::map<std::string, const Expression *> input_values;
    std::map<std::string, const ValueSymbol *> output_sources;
  };
  std::optional<clock_gatet> clock_gate;
  std::map<std::string, std::string> gate_connections;
  std::string connected_signal(std::string) const;
  std::string clock_gate_enable(const std::string &) const;
  std::string gated_clock_enable(const Expression &) const;
  bool gated_clock(const Expression &) const;
  const Expression *clock_gate_value_source(const ValueSymbol &) const;
  const ProceduralBlockSymbol *
  clock_gate_register_driver(const ValueSymbol &) const;
  enum class clock_gate_resett
  {
    External,
    Synchronized,
    Independent
  };
  std::optional<clock_gate_resett>
  clock_gate_reset_source(const async_resett &, bool allow_independent) const;
  bool certifies_clock_gate_reset(const ValueSymbol &) const;
  void verify_clock_gate_enable(const Expression &) const;
  void emit_clock_gate_branch(const clock_gate_branch_t &, exprt &);
  void load_clock_gate(const std::string &, const InstanceSymbol &);
  void emit_clock_gate(exprt &);
  void emit_clock_environment(exprt &);
  exprt gated_async_lhs(const Expression &);
  std::optional<exprt> gated_statement(
    const ProceduralBlockSymbol &,
    const TimedStatement &,
    exprt &);
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
  std::optional<exprt>
  dynamic_packed_bank_write(const AssignmentExpression &assignment);
  exprt runtime_packed_element_read(
    const ElementSelectExpression &,
    const PackedArrayType &,
    const typet &);
  ConstantRange
  packed_range(const RangeSelectExpression &, const PackedArrayType &) const;
  void require_read_only(const Expression &expr, const char *diagnostic) const;
  void check_diagnostic_argument(const Expression &argument) const;
  bool assert_diagnostic(std::string_view task) const;
  exprt diagnostic_statement(const CallExpression &call);
  symbol_exprt temporary_local(
    typet type,
    SourceLocation at,
    std::string_view stem);
  void merge_procedural_locals(
    const decltype(procedural_locals) &entered,
    const decltype(procedural_locals) &other);
  void require_empty_final(const Statement &s) const;
  exprt stmt(const Statement &s, bool at_initial_entry = false);
  std::optional<exprt> named_clocked_loop(const BlockStatement &);
  exprt counted_loop(
    const VariableSymbol &index,
    const ForLoopStatement &loop,
    const AssignmentExpression *external_init = nullptr);
  exprt declare_local(const VariableSymbol &variable);
  bool in_package_function() const;
  bool is_function_variable(const Symbol *symbol) const;
  std::string function_variable_name(const Symbol &symbol) const;
  exprt declare_function_local(const VariableSymbol &variable);
  exprt runtime_call(const CallExpression &call);
  exprt clock_reset_block(const TimedStatement &block);
  exprt procedural_assertion(
    SourceLocation at,
    const std::string &base,
    const std::string &id,
    exprt condition,
    std::string_view diagnostic_task = {});
  bool initial_parameter_check(const ImmediateAssertionStatement &) const;
  exprt immediate(
    const ImmediateAssertionStatement &s,
    const Symbol *label,
    bool at_initial_entry);
  exprt assertion(const AssertionExpr &a, SourceLocation at, irept &references);
  std::string clock(const TimingControl *ev, SourceLocation at);
  void module(const InstanceSymbol &inst, bool top);
  void finish_signal_metadata();
  using port_mapt = std::map<const Symbol *, const PortSymbol *>;
  void members(const Scope &body, const port_mapt &ports, exprt &items);
  void property(
    const ConcurrentAssertionStatement &s,
    const Symbol &label,
    exprt &items);
  void declare(const ValueSymbol &s, const PortSymbol *p, exprt &items);
  symbolt &add(const Symbol &s, const typet &t, std::string id = {});
};
} // namespace vv
