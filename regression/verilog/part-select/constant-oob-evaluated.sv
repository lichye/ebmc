// The selected operand of a constant ?: is evaluated: a constant
// out-of-range read there is reported.
module main #(parameter W = 6) (input logic [W-1:0] data_in, output logic d);
  assign d = (W > 2) ? data_in[W] : 0;
endmodule
