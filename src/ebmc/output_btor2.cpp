#include "output_btor2.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/bitvector_types.h>
#include <util/expr_iterator.h>
#include <util/simplify_expr.h>

#include <temporal-logic/temporal_logic.h>

#include "ebmc_error.h"
#include "ebmc_properties.h"
#include "transition_system.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace
{
using idt = std::size_t;

[[noreturn]] void unsupported(const exprt &expr)
{
  throw ebmc_errort{}.with_location(expr.source_location())
    << "BTOR2: unsupported expression '" << expr.id() << "'";
}

std::size_t width(const typet &type)
{
  if(type.id() == ID_bool)
    return 1;
  if(
    type.id() == ID_unsignedbv || type.id() == ID_signedbv ||
    type.id() == ID_bv)
  {
    const auto bits = to_bitvector_type(type).get_width();
    if(bits != 0)
      return bits;
  }
  throw ebmc_errort{} << "BTOR2: unsupported type '" << type.id() << "'";
}

std::size_t index_width(const array_typet &type)
{
  if(const auto size = numeric_cast<mp_integer>(type.size()))
    return address_bits(*size);
  if(type.size().id() == ID_infinity)
    return width(type.size().type());
  throw ebmc_errort{} << "BTOR2: unsupported array size";
}

std::string btor_symbol(const irep_idt &name)
{
  // BTOR2 symbols cannot contain whitespace or the comment delimiter.
  std::string result;
  for(unsigned char c : id2string(name))
  {
    if(c > ' ' && c <= '~' && c != ';' && c != '%')
      result += c;
    else
    {
      const char hex[] = "0123456789abcdef";
      result += '%';
      result += hex[c >> 4];
      result += hex[c & 15];
    }
  }
  return result;
}

template <typename F>
void for_each_conjunct(const exprt &expr, const F &f)
{
  if(expr.id() == ID_and)
  {
    for(const auto &operand : expr.operands())
      for_each_conjunct(operand, f);
  }
  else if(!expr.is_true())
    f(expr);
}

bool contains_next(const exprt &expr)
{
  return std::any_of(
    expr.unique_depth_begin(),
    expr.unique_depth_end(),
    [](const exprt &sub) { return sub.id() == ID_next_symbol; });
}

const std::map<irep_idt, std::string> word_ops{
  {ID_and, "and"},
  {ID_bitand, "and"},
  {ID_or, "or"},
  {ID_bitor, "or"},
  {ID_xor, "xor"},
  {ID_bitxor, "xor"},
  {ID_nand, "and"},
  {ID_bitnand, "and"},
  {ID_nor, "or"},
  {ID_bitnor, "or"},
  {ID_xnor, "xor"},
  {ID_bitxnor, "xor"},
  {ID_not, "not"},
  {ID_bitnot, "not"},
  {ID_implies, "implies"},
  {ID_equal, "eq"},
  {ID_notequal, "neq"},
  {ID_plus, "add"},
  {ID_minus, "sub"},
  {ID_mult, "mul"},
  {ID_unary_minus, "neg"},
  {ID_reduction_and, "redand"},
  {ID_reduction_or, "redor"},
  {ID_reduction_xor, "redxor"},
  {ID_if, "ite"},
};

class btor2_writert
{
public:
  explicit btor2_writert(const transition_systemt &system)
    : symbols(system.symbol_table)
  {
  }

  std::string write(const transition_systemt &, const ebmc_propertiest &);

private:
  using expression_mapt = std::unordered_map<exprt, idt, irep_hash>;

  std::ostringstream out;
  idt next_id = 1;
  const symbol_tablet &symbols;
  std::map<std::size_t, idt> bitvec_sorts;
  std::map<std::pair<idt, idt>, idt> array_sorts;
  std::map<std::tuple<std::string, idt, std::string>, idt> terms;
  std::map<irep_idt, exprt> definitions, init_values, next_values;
  std::map<irep_idt, idt> declared;
  std::set<irep_idt> resolving;
  expression_mapt expressions, transition_expressions, arbitrary_values;
  std::map<std::pair<irep_idt, idt>, idt> nondet_inputs;
  std::map<idt, idt> previous_values;
  idt started = 0;

  idt line(const std::string &);
  idt bitvec(std::size_t);
  idt sort(const typet &);
  idt term(const std::string &, idt, const std::string &);
  idt term(const std::string &, idt, std::initializer_list<idt>);
  idt constant(std::size_t, const mp_integer &);
  idt resize(idt, std::size_t, std::size_t, bool sign_extend = false);
  idt index(const exprt &, std::size_t, bool);
  idt shift_amount(const exprt &, std::size_t, bool, bool);
  idt symbol(const irep_idt &);
  idt previous(idt, idt);
  idt start_flag();
  idt arbitrary(const exprt &, idt, bool);
  idt convert(const exprt &, bool previous_frame = false);
  idt convert_expr(const exprt &, bool);
  void assignment(const std::string &, idt, idt, idt);
};

idt btor2_writert::line(const std::string &text)
{
  const auto id = next_id++;
  out << id << ' ' << text << '\n';
  return id;
}

idt btor2_writert::bitvec(std::size_t bits)
{
  auto &id = bitvec_sorts[bits];
  if(id == 0)
    id = line("sort bitvec " + std::to_string(bits));
  return id;
}

idt btor2_writert::sort(const typet &type)
{
  if(type.id() != ID_array)
    return bitvec(width(type));
  const auto &array = to_array_type(type);
  if(array.element_type().id() == ID_array)
    throw ebmc_errort{} << "BTOR2: unsupported nested array";
  const auto index_sort = bitvec(index_width(array));
  const auto element_sort = sort(array.element_type());
  auto &id = array_sorts[{index_sort, element_sort}];
  if(id == 0)
    id = line(
      "sort array " + std::to_string(index_sort) + " " +
      std::to_string(element_sort));
  return id;
}

idt btor2_writert::term(
  const std::string &op,
  idt type,
  const std::string &args)
{
  auto &id = terms[{op, type, args}];
  if(id == 0)
    id = line(op + " " + std::to_string(type) + args);
  return id;
}

idt btor2_writert::term(
  const std::string &op,
  idt type,
  std::initializer_list<idt> args)
{
  std::string suffix;
  for(auto arg : args)
    suffix += " " + std::to_string(arg);
  return term(op, type, suffix);
}

idt btor2_writert::constant(std::size_t bits, const mp_integer &value)
{
  return term("const", bitvec(bits), " " + integer2binary(value, bits));
}

idt btor2_writert::resize(
  idt value,
  std::size_t from,
  std::size_t to,
  bool sign_extend)
{
  if(from == to)
    return value;
  if(from < to)
    return term(sign_extend ? "sext" : "uext", bitvec(to), {value, to - from});
  return term("slice", bitvec(to), {value, to - 1, 0});
}

idt btor2_writert::index(const exprt &expr, std::size_t bits, bool prev)
{
  if(const auto value = numeric_cast<mp_integer>(expr))
    return constant(bits, *value);
  return resize(
    convert(expr, prev),
    width(expr.type()),
    bits,
    expr.type().id() == ID_signedbv);
}

idt btor2_writert::shift_amount(
  const exprt &expr,
  std::size_t bits,
  bool rotate,
  bool prev)
{
  if(const auto value = numeric_cast<mp_integer>(expr);
     value && expr.type().id() == ID_integer)
  {
    if(*value < 0)
      unsupported(expr);
    if(rotate)
      return constant(bits, *value % bits);
    return constant(bits, std::min(*value, mp_integer{bits}));
  }
  const auto from = width(expr.type());
  auto value = convert(expr, prev);
  if(from <= bits)
    return resize(value, from, bits);
  if(rotate)
  {
    value = term("urem", bitvec(from), {value, constant(from, bits)});
    return resize(value, from, bits);
  }
  // BTOR2 shift operands have equal widths. Never truncate a large shift
  // into a small one: an amount at least as large as the word saturates.
  const auto high = term("slice", bitvec(from - bits), {value, from - 1, bits});
  const auto large = term("redor", bitvec(1), {high});
  return term(
    "ite",
    bitvec(bits),
    {large, constant(bits, bits), resize(value, from, bits)});
}

idt btor2_writert::symbol(const irep_idt &name)
{
  if(auto it = declared.find(name); it != declared.end())
    return it->second;

  const auto &sym = symbols.lookup_ref(name);
  if(!resolving.insert(name).second)
    throw ebmc_errort{} << "BTOR2: unsupported cyclic definition of " << name;
  idt result;
  if(auto def = definitions.find(name); def != definitions.end())
    result = convert(def->second);
  else
  {
    const auto type = sort(sym.type);
    idt initial = 0;
    if(auto init = init_values.find(name); init != init_values.end())
      initial = convert(init->second);
    result = line(
      std::string(sym.is_state_var ? "state " : "input ") +
      std::to_string(type) + " " + btor_symbol(name));
    if(initial != 0)
      assignment("init", type, result, initial);
  }
  resolving.erase(name);
  declared.emplace(name, result);
  return result;
}

idt btor2_writert::previous(idt value, idt type)
{
  auto &id = previous_values[value];
  if(id == 0)
  {
    id = line("state " + std::to_string(type));
    assignment("next", type, id, value);
  }
  return id;
}

idt btor2_writert::start_flag()
{
  if(started == 0)
  {
    const auto type = bitvec(1);
    const auto initial = constant(1, 0);
    started = line("state " + std::to_string(type));
    assignment("init", type, started, initial);
    assignment("next", type, started, constant(1, 1));
  }
  return started;
}

idt btor2_writert::arbitrary(const exprt &expr, idt type, bool prev)
{
  auto &input = arbitrary_values[expr];
  if(input == 0)
    input = line("input " + std::to_string(type));
  return prev ? previous(input, type) : input;
}

idt btor2_writert::convert(const exprt &expr, bool prev)
{
  auto &cache = prev ? transition_expressions : expressions;
  if(auto it = cache.find(expr); it != cache.end())
    return it->second;
  const auto id = convert_expr(expr, prev);
  cache.emplace(expr, id);
  return id;
}

idt btor2_writert::convert_expr(const exprt &expr, bool prev)
{
  const auto &op = expr.id();
  if(op == ID_symbol)
  {
    const auto node = symbol(to_symbol_expr(expr).get_identifier());
    return prev ? previous(node, sort(expr.type())) : node;
  }
  if(op == ID_next_symbol && prev)
    return symbol(expr.get(ID_identifier));
  if(op == ID_nondet_symbol)
  {
    const auto type = sort(expr.type());
    auto &node = nondet_inputs[{expr.get(ID_identifier), type}];
    if(node == 0)
      node = line("input " + std::to_string(type));
    return prev ? previous(node, type) : node;
  }

  auto operand = [&](std::size_t n)
  { return convert(expr.operands().at(n), prev); };

  if(op == ID_constant)
  {
    if(expr.is_true() || expr.is_false())
      return constant(1, expr.is_true() ? 1 : 0);
    if(expr.type().id() == ID_bv)
    {
      const auto bits = width(expr.type());
      return constant(
        bits, bvrep2integer(to_constant_expr(expr).get_value(), bits, false));
    }
    const auto value = numeric_cast<mp_integer>(expr);
    if(!value)
      unsupported(expr);
    return constant(width(expr.type()), *value);
  }
  if(op == ID_typecast || op == ID_zero_extend)
  {
    const auto &source = expr.operands()[0].type();
    if(source.id() == ID_array && expr.type().id() == ID_array)
    {
      if(sort(source) != sort(expr.type()))
        unsupported(expr);
      return operand(0);
    }
    const auto bits = width(expr.type());
    if(const auto value = numeric_cast<mp_integer>(expr.operands()[0]);
       value && source.id() == ID_integer)
      return constant(bits, expr.type().id() == ID_bool ? *value != 0 : *value);
    const auto from = width(source);
    if(expr.type().id() == ID_bool && from > 1)
      return term("redor", bitvec(1), {operand(0)});
    const bool sign_extend = op == ID_typecast && source.id() == ID_signedbv;
    return resize(operand(0), from, bits, sign_extend);
  }
  if(op == ID_extractbit || op == ID_extractbits)
  {
    const auto from = width(expr.operands()[0].type());
    const auto bits = width(expr.type());
    const auto lower = numeric_cast<std::size_t>(expr.operands()[1]);
    if(lower && *lower <= from && bits <= from - *lower)
      return term(
        "slice", bitvec(bits), {operand(0), *lower + bits - 1, *lower});
    if(op == ID_extractbits)
      unsupported(expr);

    // As in EBMC's bit-vector solver, an out-of-range bit select is free.
    const auto &offset = expr.operands()[1];
    if(offset.is_constant())
      return arbitrary(expr, bitvec(1), prev);
    const auto index_bits = std::max(address_bits(from), width(offset.type()));
    const auto offset_id = index(offset, index_bits, prev);
    const auto shifted = term(
      "srl", bitvec(from), {operand(0), resize(offset_id, index_bits, from)});
    const auto selected = term("slice", bitvec(1), {shifted, 0, 0});
    if(mp_integer{from} == power(2, index_bits))
      return selected;
    const auto valid =
      term("ult", bitvec(1), {offset_id, constant(index_bits, from)});
    return term(
      "ite", bitvec(1), {valid, selected, arbitrary(expr, bitvec(1), prev)});
  }
  if(op == ID_replication)
    return convert(to_replication_expr(expr).lower(), prev);

  if(op == ID_concatenation)
  {
    auto result = operand(0);
    auto bits = width(expr.operands()[0].type());
    for(std::size_t i = 1; i < expr.operands().size(); ++i)
    {
      bits += width(expr.operands()[i].type());
      result = term("concat", bitvec(bits), {result, operand(i)});
    }
    if(bits != width(expr.type()))
      unsupported(expr);
    return result;
  }
  if(
    op == ID_shl || op == ID_lshr || op == ID_ashr || op == ID_rol ||
    op == ID_ror)
  {
    const auto bits = width(expr.type());
    const auto *tag = op == ID_shl    ? "sll"
                      : op == ID_lshr ? "srl"
                      : op == ID_ashr ? "sra"
                      : op == ID_rol  ? "rol"
                                      : "ror";
    const auto amount = shift_amount(
      expr.operands()[1], bits, op == ID_rol || op == ID_ror, prev);
    return term(tag, bitvec(bits), {operand(0), amount});
  }
  if(op == ID_index)
  {
    const auto &array = to_array_type(expr.operands()[0].type());
    return term(
      "read",
      sort(expr.type()),
      {operand(0), index(expr.operands()[1], index_width(array), prev)});
  }
  if(op == ID_with && expr.type().id() == ID_array)
  {
    const auto &array = to_array_type(expr.type());
    auto result = operand(0);
    for(std::size_t i = 1; i + 1 < expr.operands().size(); i += 2)
      result = term(
        "write",
        sort(array),
        {result,
         index(expr.operands()[i], index_width(array), prev),
         operand(i + 1)});
    return result;
  }
  if(op == ID_array || op == ID_array_of)
  {
    const auto &array = to_array_type(expr.type());
    const auto bits = index_width(array);
    const auto type = sort(array);
    auto result = arbitrary(expr, type, prev);
    const auto count = numeric_cast<std::size_t>(array.size());
    if(!count || (op == ID_array && *count != expr.operands().size()))
      unsupported(expr);
    for(std::size_t i = 0; i < *count; ++i)
      result = term(
        "write",
        type,
        {result, constant(bits, i), operand(op == ID_array ? i : 0)});
    return result;
  }
  if(op == ID_lt || op == ID_le || op == ID_gt || op == ID_ge)
  {
    const auto *relation = op == ID_lt   ? "lt"
                           : op == ID_le ? "lte"
                           : op == ID_gt ? "gt"
                                         : "gte";
    const auto prefix =
      expr.operands()[0].type().id() == ID_signedbv ? 's' : 'u';
    return term(
      prefix + std::string(relation), bitvec(1), {operand(0), operand(1)});
  }
  if(op == ID_div || op == ID_mod)
  {
    const bool is_signed = expr.type().id() == ID_signedbv;
    const auto *tag = op == ID_div ? (is_signed ? "sdiv" : "udiv")
                                   : (is_signed ? "srem" : "urem");
    const auto type = sort(expr.type());
    const auto divisor = operand(1);
    const auto value = term(tag, type, {operand(0), divisor});
    if(const auto n = numeric_cast<mp_integer>(expr.operands()[1]);
       n && *n != 0)
      return value;
    // EBMC leaves division and remainder by zero unconstrained.
    const auto div_by_zero =
      term("eq", bitvec(1), {divisor, constant(width(expr.type()), 0)});
    return term("ite", type, {div_by_zero, arbitrary(expr, type, prev), value});
  }
  if(op == ID_unary_plus)
    return operand(0);
  if(
    op == ID_reduction_nand || op == ID_reduction_nor ||
    op == ID_reduction_xnor)
  {
    const auto *tag = op == ID_reduction_nand  ? "redand"
                      : op == ID_reduction_nor ? "redor"
                                               : "redxor";
    return term("not", bitvec(1), {term(tag, bitvec(1), {operand(0)})});
  }
  if(auto it = word_ops.find(op); it != word_ops.end())
  {
    const auto type = sort(expr.type());
    const auto &tag = it->second;
    const auto arity = expr.operands().size();
    if(arity == 1)
      return term(tag, type, {operand(0)});
    if(arity == 3 && op == ID_if)
      return term(tag, type, {operand(0), operand(1), operand(2)});
    auto result = operand(0);
    for(std::size_t i = 1; i < arity; ++i)
      result = term(tag, type, {result, operand(i)});
    if(
      op == ID_nand || op == ID_bitnand || op == ID_nor || op == ID_bitnor ||
      op == ID_xnor || op == ID_bitxnor)
      result = term("not", type, {result});
    return result;
  }
  unsupported(expr);
}

void btor2_writert::assignment(
  const std::string &tag,
  idt type,
  idt state,
  idt value)
{
  line(
    tag + " " + std::to_string(type) + " " + std::to_string(state) + " " +
    std::to_string(value));
}

std::string btor2_writert::write(
  const transition_systemt &system,
  const ebmc_propertiest &properties)
{
  // Validate property shape before converting potentially large designs.
  for(const auto &property : properties.properties)
  {
    if(property.is_disabled())
      continue;
    const auto &expr = property.normalized_expr;
    if(
      (expr.id() != ID_G && expr.id() != ID_AG && expr.id() != ID_sva_always) ||
      has_temporal_operator(to_unary_expr(expr).op()) ||
      property.is_exists_path())
      throw ebmc_errort{}.with_location(property.location)
        << "BTOR2: unsupported "
        << (property.is_assumption() ? "assumption" : "property") << " '"
        << property.name << "' (expected always with a non-temporal operand)";
  }

  const auto &trans = system.trans_expr;
  std::vector<exprt> invariants, initial, transitions;
  for_each_conjunct(
    trans.invar(),
    [&](const exprt &expr)
    {
      if(expr.id() == ID_equal && expr.operands()[0].id() == ID_symbol)
      {
        const auto &name = expr.operands()[0].get(ID_identifier);
        const auto &sym = symbols.lookup_ref(name);
        if(
          !sym.is_state_var && !sym.is_input &&
          definitions.emplace(name, expr.operands()[1]).second)
          return;
      }
      invariants.push_back(expr);
    });
  for_each_conjunct(
    trans.init(),
    [&](const exprt &expr)
    {
      if(expr.id() == ID_equal && expr.operands()[0].id() == ID_symbol)
      {
        const auto &name = expr.operands()[0].get(ID_identifier);
        const auto &symbol = symbols.lookup_ref(name);
        if(symbol.is_state_var && symbol.type.id() != ID_array)
        {
          auto value = simplify_expr(expr.operands()[1], namespacet{symbols});
          if(
            value.is_constant() &&
            init_values.emplace(name, std::move(value)).second)
            return;
        }
      }
      initial.push_back(expr);
    });
  for_each_conjunct(
    trans.trans(),
    [&](const exprt &expr)
    {
      if(
        expr.id() == ID_equal && expr.operands()[0].id() == ID_next_symbol &&
        !contains_next(expr.operands()[1]))
      {
        const auto &name = expr.operands()[0].get(ID_identifier);
        if(
          symbols.lookup_ref(name).is_state_var &&
          next_values.emplace(name, expr.operands()[1]).second)
          return;
      }
      transitions.push_back(expr);
    });

  for(const auto &state : system.state_variables())
  {
    const auto type = sort(state.type());
    const auto id = symbol(state.get_identifier());
    const auto next = next_values.find(state.get_identifier());
    const auto value = next == next_values.end()
                         ? line("input " + std::to_string(type))
                         : convert(next->second);
    assignment("next", type, id, value);
  }
  for(const auto &expr : invariants)
    line("constraint " + std::to_string(convert(expr)));
  for(const auto &expr : initial)
  {
    const auto constraint =
      term("or", bitvec(1), {start_flag(), convert(expr)});
    line("constraint " + std::to_string(constraint));
  }
  for(const auto &expr : transitions)
  {
    // A transition only constrains pairs of frames. Check it at the later
    // frame, using saved values for the earlier frame, and skip frame zero.
    const auto constraint =
      term("implies", bitvec(1), {start_flag(), convert(expr, true)});
    line("constraint " + std::to_string(constraint));
  }
  for(const auto &property : properties.properties)
  {
    if(property.is_disabled())
      continue;
    const auto value = convert(to_unary_expr(property.normalized_expr).op());
    if(property.is_assumption())
      line("constraint " + std::to_string(value));
    else
    {
      const auto bad = term("not", bitvec(1), {value});
      line("bad " + std::to_string(bad) + " " + btor_symbol(property.name));
    }
  }
  return out.str();
}
} // namespace

std::string output_btor2(
  const transition_systemt &system,
  const ebmc_propertiest &properties)
{
  return btor2_writert{system}.write(system, properties);
}
