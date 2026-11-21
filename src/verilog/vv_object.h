#ifndef EBMC_VERILOG_VV_OBJECT_H
#define EBMC_VERILOG_VV_OBJECT_H

#include <util/symbol_table.h>
#include <string>

// Experiment schema. Stable identities are fully-qualified symbol names.
struct vv_objectt
{
  symbol_tablet symbols;
  irept metadata{"design"};
  void write(const std::string &path) const;
  static vv_objectt read(const std::string &path);
  void validate(bool require_constraints) const;
};
#endif
