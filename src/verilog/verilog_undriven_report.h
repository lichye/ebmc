#ifndef CPROVER_VERILOG_VERILOG_UNDRIVEN_REPORT_H
#define CPROVER_VERILOG_VERILOG_UNDRIVEN_REPORT_H

#include <util/source_location.h>

#include <functional>
#include <string>
#include <vector>

struct verilog_undriven_nett
{
  std::string name;
  source_locationt location;
};

/// Called with the read-but-undriven nets and slices after building RTL.
/// The callback may throw to stop before transition relation conversion.
using verilog_undriven_reportert =
  std::function<void(const std::vector<verilog_undriven_nett> &)>;

#endif // CPROVER_VERILOG_VERILOG_UNDRIVEN_REPORT_H
