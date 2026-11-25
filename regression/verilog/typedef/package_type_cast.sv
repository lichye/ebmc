package fpnew_pkg;
  typedef enum logic [2:0] { FMT0 = 3'd0, FMT3 = 3'd3 } fp_format_e;
endpackage

package other_pkg;
  typedef enum logic [4:0] { OTHER31 = 5'd31 } fp_format_e;
endpackage

module main(input logic clk, rst_n);
  import other_pkg::*;
  fpnew_pkg::fp_format_e fmt;

`ifdef BAD_UNKNOWN
  localparam fpnew_pkg::fp_format_e bad = fpnew_pkg::missing_t'(0);
`elsif BAD_TARGET
  localparam fp_format_e bad = fpnew_pkg::fp_format_e'(0);
`else
  localparam fp_format_e other = fp_format_e'(31);

`define LOAD(q, reset_value, next_value) \
  always_ff @(posedge clk or negedge rst_n) \
    if (!rst_n) q <= reset_value; else q <= next_value;
  `LOAD(fmt, fpnew_pkg::fp_format_e'(0), fpnew_pkg::fp_format_e'(8'h2b))
`undef LOAD

  p_width: assert property (@(posedge clk)
    $bits(fpnew_pkg::fp_format_e'(0)) == 3 && $bits(other) == 5);
  p_scope: assert property (@(posedge clk) other == OTHER31);
  p_reset: assert property (@(posedge clk) !rst_n |=> fmt == fpnew_pkg::FMT0);
  p_capture: assert property (@(posedge clk) rst_n |=> fmt == fpnew_pkg::FMT3);
`endif
endmodule
