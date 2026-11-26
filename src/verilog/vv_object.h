#ifndef EBMC_VERILOG_VV_OBJECT_H
#define EBMC_VERILOG_VV_OBJECT_H

#include <util/symbol_table.h>

#include <string>

// Experiment schema. Stable identities are fully-qualified symbol names.
struct vv_objectt
{
  static constexpr auto clock_gate_v1 = "coral-w4-env1-env2.v1";
  static constexpr auto clock_gate_sampling =
    "posedge-preponed/after-edge-external-reset/async-priority";
  static constexpr auto clock_gate_independent_reset_sampling =
    "parent-edge-NBA/async-priority/no-ENV2";
  bool has_clock_gate_branches() const
  {
    return !metadata.find("clock_gate_contract")
              .find("ClockGate")
              .get_named_sub()
              .empty();
  }
  bool has_clock_gate() const
  {
    return metadata.find("clock_gate_contract").is_not_nil();
  }
  std::string clock_gate_description() const;
  symbol_tablet symbols;
  irept metadata{"design"};
  void write(const std::string &path) const;
  static vv_objectt read(const std::string &path);
  void validate(bool require_constraints) const;
};
#endif
