/*******************************************************************\

Module: EBMC Property Checker

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#include "property_checker.h"

#include <util/output_file.h>
#include <util/string2int.h>

#include <new-ic3/new_ic3_engine.h>
#include <solvers/flattening/boolbv.h>
#include <trans-word-level/instantiate_word_level.h>
#include <trans-word-level/lasso.h>
#include <solvers/sat/satcheck.h>
#include <trans-netlist/trans_trace_netlist.h>
#include <trans-netlist/unwind_netlist.h>

#include "bdd_engine.h"
#include "bmc.h"
#include "dimacs_writer.h"
#include "ebmc_error.h"
#include "ebmc_solver_factory.h"
#include "engine_heuristic.h"
#include "ic3_engine.h"
#include "instrument_past.h"
#include "instrument_buechi.h"
#include "k_induction.h"
#include "netlist.h"
#include "report_results.h"
#include "witness_lasso.h"

#include <charconv>
#include <chrono>
#include <iostream>
#include <limits>

property_checker_resultt word_level_bmc(
  const cmdlinet &cmdline,
  const transition_systemt &transition_system,
  const ebmc_propertiest &properties,
  message_handlert &message_handler)
{
  auto solver_factory = ebmc_solver_factory(cmdline);

  bool convert_only = cmdline.isset("smt2") || cmdline.isset("outfile") ||
                      cmdline.isset("show-formula");

  try
  {
    if(cmdline.isset("max-bound"))
    {
      if(convert_only)
        throw ebmc_errort() << "please set a specific bound";

      const std::size_t max_bound =
        unsafe_string2size_t(cmdline.get_value("max-bound"));

      for(std::size_t bound = 1; bound <= max_bound; bound++)
      {
        messaget message{message_handler};
        message.status() << "Doing BMC with bound " << bound << messaget::eom;

#if 0
        const namespacet ns(transition_system.symbol_table);
        CHECK_RETURN(trans_expr.has_value());
        word_level_unwind_optionst unwind_options;
        unwind_options.add_initial_state = true;
        ::unwind(
          *trans_expr, *message_handler, solver, bound + 1, ns, unwind_options);
        result=finish_word_level_bmc(solver);
#endif
      }

      return property_checker_resultt{properties};
    }
    else
    {
      std::size_t bound;

      if(cmdline.isset("bound"))
      {
        bound = unsafe_string2unsigned(cmdline.get_value("bound"));
      }
      else
      {
        messaget message{message_handler};
        message.warning() << "using default bound 1" << messaget::eom;
        bound = 1;
      }

      if(!convert_only)
        if(properties.properties.empty())
          throw "no properties";

      bool bmc_with_assumptions = cmdline.isset("bmc-with-assumptions");
      bool cone_of_influence = cmdline.isset("coi");

      auto result = bmc(
        bound,
        convert_only,
        bmc_with_assumptions,
        cone_of_influence,
        transition_system,
        properties,
        solver_factory,
        message_handler);

      if(convert_only)
        return property_checker_resultt::success();

      return result;
    }
  }

  catch(const char *e)
  {
    messaget message{message_handler};
    message.error() << e << messaget::eom;
    return property_checker_resultt::error();
  }

  catch(const std::string &e)
  {
    messaget message{message_handler};
    message.error() << e << messaget::eom;
    return property_checker_resultt::error();
  }

  catch(int)
  {
    return property_checker_resultt::error();
  }
}

// Translate finite Boolean/bit-vector obligations against the actual AIG
// timeframe map. Do not let the bit-blaster invent independent inputs.
static std::optional<bvt> bounded_netlist_obligations(
  const ebmc_propertiest::propertyt &property,
  const var_mapt &variables,
  const bmc_mapt &frames,
  std::size_t bound,
  const namespacet &ns,
  cnft &solver,
  message_handlert &messages)
{
  if(!bmc_supports_property(property.normalized_expr))
    return {};
  auto obligations = ::property(
    property.normalized_expr, !property.is_exists_path(), messages, bound + 1);
  if(uses_lasso_symbol(obligations))
    return {};

  std::map<irep_idt, std::pair<const var_mapt::vart *, std::size_t>> bindings;
  property.normalized_expr.visit_pre([&](const exprt &sub) {
    if(sub.id() != ID_symbol)
      return;
    auto found = variables.map.find(sub.get(ID_identifier));
    if(found != variables.map.end())
      for(std::size_t t=0;t<=bound;++t)
        bindings.emplace(timeframe_identifier(t,found->first),std::make_pair(&found->second,t));
  });

  boolbvt bv{ns, solver, messages};
  std::set<irep_idt> bound_ids;
  bool supported = true;
  for(const auto &obligation : obligations)
    obligation.visit_pre([&](const exprt &sub) {
      const auto id = sub.id();
      const auto type = sub.type().id();
      if(id == ID_symbol)
      {
        auto binding = bindings.find(sub.get(ID_identifier));
        if(binding == bindings.end() ||
           (type != ID_bool && type != ID_unsignedbv && type != ID_signedbv))
        {
          supported = false;
          return;
        }
        if(!bound_ids.insert(binding->first).second)
          return;
        const auto &[var, t] = binding->second;
        const bvt bits = type == ID_bool ? bvt{bv.convert(sub)} : bv.convert_bv(sub);
        if(bits.size() != var->bits.size())
        {
          supported = false;
          return;
        }
        for(std::size_t i=0;i<bits.size();++i)
          solver.set_equal(bits[i],frames.get(t,var->bits[i]));
      }
      else if(id != ID_constant && id != ID_and && id != ID_or && id != ID_not &&
              id != ID_implies && id != ID_equal && id != ID_notequal && id != ID_if &&
              id != ID_typecast && id != ID_lt && id != ID_le && id != ID_gt && id != ID_ge)
        supported = false;
    });
  if(!supported)
    return {};
  bvt literals;
  for(const auto &obligation : obligations)
    literals.push_back(bv.convert(obligation));
  return literals;
}

property_checker_resultt finish_bit_level_bmc(
  std::size_t bound,
  bool cover_duality,
  const bmc_mapt &bmc_map,
  propt &solver,
  const transition_systemt &transition_system,
  ebmc_propertiest &properties,
  message_handlert &message_handler)
{
  auto sat_start_time = std::chrono::steady_clock::now();

  messaget message{message_handler};
  message.status() << "Solving with " << solver.solver_text() << messaget::eom;

  for(auto &property : properties.properties)
  {
    if(property.is_disabled())
      continue;

    if(property.is_failure())
      continue;

    if(property.is_unsupported())
      continue;

    if(property.is_assumed())
      continue;

    message.status() << "Checking " << property.name << messaget::eom;

    literalt property_literal = !solver.land(property.timeframe_literals);

    bvt assumptions;
    assumptions.push_back(property_literal);

    propt::resultt prop_result = solver.prop_solve(assumptions);

    switch(prop_result)
    {
    case propt::resultt::P_SATISFIABLE:
    {
      if(cover_duality && property.is_exists_path())
        property.proved();
      else
        property.refuted();
      message.result() << "SAT: counterexample found" << messaget::eom;

      namespacet ns{transition_system.symbol_table};

      property.witness_trace =
        compute_trans_trace(property.timeframe_literals, bmc_map, solver, ns);
    }
    break;

    case propt::resultt::P_UNSATISFIABLE:
      message.result() << "UNSAT: No counterexample found within bound"
                       << messaget::eom;
      if(cover_duality && property.is_exists_path())
        property.refuted_with_bound(bound);
      else
        property.proved_with_bound(bound);
      break;

    case propt::resultt::P_ERROR:
      message.error() << "Error from decision procedure" << messaget::eom;
      return property_checker_resultt::error();

    default:
      message.error() << "Unexpected result from decision procedure"
                      << messaget::eom;
      return property_checker_resultt::error();
    }
  }

  auto sat_stop_time = std::chrono::steady_clock::now();

  message.statistics()
    << "Solver time: "
    << std::chrono::duration<double>(sat_stop_time - sat_start_time).count()
    << messaget::eom;

  return property_checker_resultt{properties};
}

property_checker_resultt bit_level_bmc(
  cnft &solver,
  bool convert_only,
  const cmdlinet &cmdline,
  transition_systemt &transition_system,
  ebmc_propertiest &properties,
  message_handlert &message_handler)
{
  messaget message{message_handler};

  std::size_t bound;

  if(cmdline.isset("bound"))
  {
    bound = unsafe_string2unsigned(cmdline.get_value("bound"));
  }
  else
  {
    message.warning() << "using default bound 1" << messaget::eom;
    bound = 1;
  }

  try
  {
    if(!convert_only)
      if(properties.properties.empty())
        throw "no properties";

    // make net-list
    message.status() << "Generating Netlist" << messaget::eom;

    auto netlist =
      make_netlist(transition_system, properties, cmdline, message_handler);

    message.statistics() << "Latches: " << netlist.var_map.latches.size()
                         << ", nodes: " << netlist.number_of_nodes()
                         << messaget::eom;

    messaget message{message_handler};
    message.status() << "Unwinding Netlist" << messaget::eom;

    const auto bmc_map = bmc_mapt{netlist, bound + 1, solver};

    ::unwind(netlist, bmc_map, message, solver);

    const namespacet ns(transition_system.symbol_table);

    // convert the properties
    bool assumption_unsupported = false;

    for(auto &property : properties.properties)
    {
      if(property.is_disabled())
        continue;

      if(!netlist_bmc_supports_property(property.normalized_expr))
      {
        if(!convert_only && !cmdline.isset("finite-prefix"))
        {
          const namespacet ns{transition_system.symbol_table};
          auto literals = bounded_netlist_obligations(
            property, netlist.var_map, bmc_map, bound, ns, solver, message_handler);
          if(literals)
          {
            property.timeframe_literals = std::move(*literals);
            for(auto l : property.timeframe_literals)
              if(property.is_assumed())
                solver.l_set_to_true(l);
              else
                solver.set_frozen(l);
            continue;
          }
        }
        if(property.is_assumed())
        {
          // An assumption that cannot be added to the netlist must not be
          // silently dropped, as this is unsound: properties that hold under
          // the assumption might otherwise be reported as refuted. Mark the
          // assumption as unsupported, and mark refuted properties as
          // inconclusive below.
          assumption_unsupported = true;
          property.unsupported(
            "assumption not supported by netlist BMC engine");
        }
        else
        {
          property.failure("property not supported by netlist BMC engine");
        }
        continue;
      }

      // look up the property in the netlist
      auto netlist_property = netlist.properties.find(property.identifier);
      CHECK_RETURN(netlist_property != netlist.properties.end());
      CHECK_RETURN(netlist_property->second.has_value());

      property.timeframe_literals =
        ::unwind_property(netlist_property->second.value(), bmc_map);

      if(property.is_assumed())
      {
        // force these to be true
        for(auto l : property.timeframe_literals)
          solver.l_set_to(l, true);
      }
      else
      {
        // freeze for incremental usage
        for(auto l : property.timeframe_literals)
          solver.set_frozen(l);
      }
    }

    if(convert_only)
      return property_checker_resultt::success();
    else
    {
      auto result = finish_bit_level_bmc(
        bound, !cmdline.isset("finite-prefix"), bmc_map, solver, transition_system, properties, message_handler);

      // Any refuted property is really inconclusive if there are unsupported
      // assumptions, as the assumption might have proven the property.
      if(
        assumption_unsupported &&
        result.status == property_checker_resultt::statust::VERIFICATION_RESULT)
      {
        for(auto &property : result.properties)
          if(property.is_refuted() || (!cmdline.isset("finite-prefix") &&
              property.is_exists_path() && property.is_proved()))
            property.inconclusive();
      }

      return result;
    }
  }

  catch(const char *e)
  {
    messaget message{message_handler};
    message.error() << e << messaget::eom;
    return property_checker_resultt::error();
  }

  catch(const std::string &e)
  {
    messaget message{message_handler};
    message.error() << e << messaget::eom;
    return property_checker_resultt::error();
  }

  catch(int)
  {
    return property_checker_resultt::error();
  }
}

property_checker_resultt bit_level_bmc(
  const cmdlinet &cmdline,
  transition_systemt &transition_system,
  ebmc_propertiest &properties,
  message_handlert &message_handler)
{
  if(cmdline.isset("dimacs"))
  {
    if(cmdline.isset("outfile"))
    {
      auto outfile = output_filet{cmdline.get_value("outfile")};

      messaget message{message_handler};
      message.status() << "Writing DIMACS CNF to `" << outfile.name() << "'"
                       << messaget::eom;

      dimacs_cnf_writert dimacs_cnf_writer{outfile.stream(), message_handler};

      return bit_level_bmc(
        dimacs_cnf_writer,
        true,
        cmdline,
        transition_system,
        properties,
        message_handler);
    }
    else
    {
      dimacs_cnf_writert dimacs_cnf_writer{std::cout, message_handler};

      return bit_level_bmc(
        dimacs_cnf_writer,
        true,
        cmdline,
        transition_system,
        properties,
        message_handler);
    }
  }
  else
  {
    if(cmdline.isset("outfile"))
      throw ebmc_errort()
        << "Cannot write to outfile without file format option";

    satcheckt satcheck{message_handler};

    messaget message{message_handler};
    message.status() << "Using " << satcheck.solver_text() << messaget::eom;

    return bit_level_bmc(
      satcheck, false, cmdline, transition_system, properties, message_handler);
  }
}

property_checker_resultt property_checker(
  const cmdlinet &cmdline,
  transition_systemt &transition_system,
  ebmc_propertiest &properties,
  message_handlert &message_handler)
{
  const bool strict = !cmdline.isset("finite-prefix");
  const bool converting = cmdline.isset("dimacs") || cmdline.isset("smt2") ||
                          cmdline.isset("outfile") || cmdline.isset("show-formula") ||
                          cmdline.isset("show-bdds");
  const bool unbounded_engine = cmdline.isset("bdd") || cmdline.isset("ic3") ||
                                cmdline.isset("new-ic3") || cmdline.isset("k-induction");
  std::optional<std::size_t> bmc_bound;
  std::size_t witness_bound = 48;
  if(strict && !converting && (cmdline.isset("aig") ||
       (cmdline.isset("bound") && !unbounded_engine)))
  {
    const std::string arg = cmdline.value_opt("bound").value_or("1");
    unsigned depth;
    const auto parsed = std::from_chars(arg.data(), arg.data()+arg.size(), depth);
    if(parsed.ec != std::errc{} || parsed.ptr != arg.data()+arg.size() ||
       std::size_t(depth) >= std::numeric_limits<std::size_t>::max()-48)
      throw ebmc_errort{} << "invalid BMC bound";
    bmc_bound = depth;
    witness_bound += depth;
  }
  if(cmdline.isset("witness-lasso"))
  {
    if(!strict)
      throw ebmc_errort{} << "--finite-prefix conflicts with --witness-lasso";

    const auto &arg = cmdline.get_value("witness-lasso");
    std::size_t bound;
    auto parsed = std::from_chars(arg.data(), arg.data() + arg.size(), bound);
    if(
      parsed.ec != std::errc{} || parsed.ptr != arg.data() + arg.size() ||
      bound == 0 || bound == std::numeric_limits<std::size_t>::max())
      throw ebmc_errort{}
        << "--witness-lasso requires a positive transition bound";
    witness_bound = bound;
  }

  bool use_heuristic_engine =
    !cmdline.isset("bdd") && !cmdline.isset("aig") &&
    !cmdline.isset("k-induction") && !cmdline.isset("ic3") &&
    !cmdline.isset("new-ic3") && !cmdline.isset("bound");

  if(strict && !converting && !bmc_bound &&
     (use_heuristic_engine || cmdline.isset("new-ic3") || cmdline.isset("k-induction")))
  {
    try
    {
      instrument_buechi(transition_system, properties, true, message_handler, true);
    }
    catch(const ebmc_errort &error)
    {
      // Let the native engine diagnose any remaining unsupported assumptions.
      messaget{message_handler}.warning() << error.what() << messaget::eom;
    }
  }

  if(cmdline.isset("k-induction") || use_heuristic_engine)
  {
    // The step case of k-induction can't do $past
    instrument_past(transition_system, properties);
  }

  auto result = [&]() -> property_checker_resultt
  {
    if(cmdline.isset("bdd") || cmdline.isset("show-bdds"))
    {
      return bdd_engine(
        cmdline, transition_system, properties, message_handler);
    }
    else if(cmdline.isset("aig") || cmdline.isset("dimacs"))
    {
      // bit-level BMC
      return bit_level_bmc(
        cmdline, transition_system, properties, message_handler);
    }
    else if(cmdline.isset("k-induction"))
    {
      return k_induction(
        cmdline, transition_system, properties, message_handler);
    }
    else if(cmdline.isset("ic3"))
    {
#ifdef _WIN32
      throw ebmc_errort() << "No support for IC3 on Windows";
#else
      return ic3_engine(
        cmdline, transition_system, properties, message_handler);
#endif
    }
    else if(cmdline.isset("new-ic3"))
    {
#ifdef _WIN32
      throw ebmc_errort() << "No support for new IC3 on Windows";
#else
      return new_ic3_engine(
        cmdline, transition_system, properties, message_handler);
#endif
    }
    else if(cmdline.isset("bound"))
    {
      // word-level BMC
      return word_level_bmc(
        cmdline, transition_system, properties, message_handler);
    }
    else
    {
      // heuristic engine selection
      return engine_heuristic(
        cmdline, transition_system, properties, message_handler);
    }
  }();

  if(result.status == property_checker_resultt::statust::VERIFICATION_RESULT)
  {
    const namespacet ns{transition_system.symbol_table};
    if(strict && !converting)
      certify_witnesses(
        cmdline, transition_system, result, witness_bound, bmc_bound, message_handler);
    report_results(cmdline, use_heuristic_engine, result, ns, message_handler);
  }

  return result;
}

int property_checker_resultt::exit_code() const
{
  switch(status)
  {
  case statust::ERROR:
    return 10;

  case statust::SUCCESS:
    return 0;

  case statust::VERIFICATION_RESULT:
    // We return '0' if all properties are proved,
    // and '10' otherwise.
    return all_properties_proved() ? 0 : 10;
  }

  UNREACHABLE;
}
