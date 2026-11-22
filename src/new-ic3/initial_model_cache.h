#ifndef CPROVER_NEW_IC3_INITIAL_MODEL_CACHE_H
#define CPROVER_NEW_IC3_INITIAL_MODEL_CACHE_H

#include <solvers/sat/satcheck.h>

#include <utility>
#include <vector>

/// One assignment for the unchanged initial-state formula of an IC3 solver.
class initial_model_cachet
{
public:
  using solvert = satcheck_no_simplifiert;
  using variablet = literalt::var_not;

  initial_model_cachet(const solvert &solver, std::vector<variablet> variables)
    : solver(&solver),
      variables(std::move(variables)),
      values(solver.no_variables(), tvt::unknown())
  {
  }

  bool matches(const solvert &current, const bvt &cube) const
  {
    if(!valid || !same_context(current))
      return false;

    for(literalt lit : cube)
    {
      if(lit.is_true())
        continue;

      if(lit.is_false() || lit.var_no() >= values.size())
        return false;

      const auto value = values[lit.var_no()];
      if(value.is_unknown() || value.is_true() == lit.sign())
        return false;
    }
    return true;
  }

  /// Read the observed bits immediately after native SAT. Each known value
  /// must come from that same model, and an incomplete capture invalidates
  /// the previous assignment.
  template <typename Read>
  void capture(const solvert &current, Read read)
  {
    valid = false;
    if(!same_context(current))
      return;

    for(auto variable : variables)
    {
      const auto value = read(literalt(variable, false));
      if(value.is_unknown())
        return;
      values[variable] = value;
    }
    valid = true;
  }

private:
  bool same_context(const solvert &current) const
  {
    return solver == &current && values.size() == current.no_variables();
  }

  const solvert *const solver;
  const std::vector<variablet> variables;
  std::vector<tvt> values;
  bool valid = false;
};

#endif
