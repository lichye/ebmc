/*******************************************************************\

Module: Extracting Counterexamples

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_TRANS_TRACE_H
#define CPROVER_TRANS_TRACE_H

#include <util/expr.h>
#include <util/namespace.h>
#include <util/threeval.h>
#include <util/ui_message.h>

class jsont;

class trans_tracet
{
public:
  class statet
  {
  public:
    class assignmentt
    {
    public:
      exprt lhs;
      exprt rhs;
      source_locationt location;

      assignmentt(
        exprt __lhs,
        exprt __rhs,
        source_locationt __location = source_locationt::nil())
        : lhs(std::move(__lhs)),
          rhs(std::move(__rhs)),
          location(std::move(__location))
      {
      }
    };

    typedef std::list<assignmentt> assignmentst;
    assignmentst assignments;
    
    bool property_failed;
    
    statet():property_failed(false)
    {
    }
  };

  // one state per timeframe
  typedef std::vector<statet> statest;
  statest states;

  // For a certified lasso the last state duplicates this earlier frame.
  std::optional<std::size_t> loop_start;
  // A finite bad prefix in a structurally total transition system.
  bool total_extension = false;

  // Finite traces end at the first violation; a lasso includes its suffix.
  std::size_t last_timeframe_to_show() const
  {
    PRECONDITION(!states.empty());
    return loop_start ? states.size() - 1
                      : get_min_failing_timeframe().value_or(states.size() - 1);
  }
  
  // mode of whole trace
  std::string mode;

  // returns the latest failing timeframe, if any
  std::optional<std::size_t> get_max_failing_timeframe() const;

  // returns the earliest failing timeframe, if any
  std::optional<std::size_t> get_min_failing_timeframe() const;
};

// outputting traces

jsont json(const trans_tracet &, const namespacet &);

xmlt xml(const trans_tracet &, const namespacet &);

void show_trans_trace(
  const trans_tracet &,
  messaget &,
  const namespacet &,
  std::ostream &);

void show_trans_trace_xml(
  const trans_tracet &trace,
  messaget &,
  const namespacet &,
  std::ostream &);

void show_trans_trace_json(
  const trans_tracet &,
  messaget &,
  const namespacet &,
  std::ostream &);

void show_trans_trace_vcd(
  const trans_tracet &,
  messaget &,
  const namespacet &,
  std::ostream &);

void show_trans_trace_numbered(
  const trans_tracet &,
  messaget &,
  const namespacet &,
  std::ostream &);

#endif
