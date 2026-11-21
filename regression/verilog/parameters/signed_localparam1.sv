module parameter_port #(parameter signed P = 8'hff)
  (output wire [7:0] width, output wire signed [31:0] value);
  assign width = $bits(P);
  assign value = P;
endmodule

module top(input clk);
  localparam signed BIAS = (1 << (8 - 1)) - 1;
  localparam signed FD = 10 - 5;
  localparam signed ELOG2_E = 4'd1;
  localparam UNS = (1 << (8 - 1)) - 1;
  localparam signed [1:0] EXPLICIT = 5;
  localparam logic signed SCALAR = 3;
  localparam signed UNKNOWN = 8'b1000_xxzz;
  localparam signed SHIFT = 8'h80 >>> 1;
  localparam signed REAL_VALUE = 1.5;

  wire [7:0] default_width, override_width;
  wire signed [31:0] default_value, override_value;
  parameter_port u_default(default_width, default_value);
  parameter_port #(.P(12'hf80)) u_override(override_width, override_value);

  p_bias: assert property (@(posedge clk) BIAS == 127 && $bits(BIAS) == 32);
  p_frac: assert property (@(posedge clk) FD == 5);
  p_elog: assert property (@(posedge clk) ELOG2_E == 1 && $bits(ELOG2_E) == 4);
  p_untyped: assert property (@(posedge clk) UNS == 127);
  p_explicit: assert property (@(posedge clk) EXPLICIT == 1 && $bits(EXPLICIT) == 2);
  p_scalar: assert property (@(posedge clk) SCALAR == -1 && $bits(SCALAR) == 1);
  p_unknown: assert property (@(posedge clk) UNKNOWN === 8'b1000_xxzz);
  p_shift: assert property (@(posedge clk) SHIFT == 64 && $bits(SHIFT) == 8);
  p_real: assert property (@(posedge clk) REAL_VALUE == 2 && $bits(REAL_VALUE) == 32);
  p_default: assert property (@(posedge clk) default_value == -1 && default_width == 8);
  p_override: assert property (@(posedge clk) override_value == -128 && override_width == 12);

  p_fail: assert property (@(posedge clk) BIAS == -1);
endmodule
