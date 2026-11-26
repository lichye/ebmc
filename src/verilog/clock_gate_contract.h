#ifndef CPROVER_VERILOG_CLOCK_GATE_CONTRACT_H
#define CPROVER_VERILOG_CLOCK_GATE_CONTRACT_H

class transition_systemt;
struct vv_objectt;

namespace ebmc
{
/// Apply the temporary clock-gate environment to a transition system built
/// from the validated object. Ordinary objects are unaffected.
/// The caller chooses whether to require reset at the initial sample.
void apply_clock_gate_contract(
  const vv_objectt &,
  transition_systemt &,
  bool start_reset);
} // namespace ebmc

#endif // CPROVER_VERILOG_CLOCK_GATE_CONTRACT_H
