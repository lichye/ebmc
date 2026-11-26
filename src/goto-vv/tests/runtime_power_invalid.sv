module power_variable_base(input logic clk, input logic [7:0] base, exponent);
  wire [7:0] y = base ** exponent;
  p: assert property (@(posedge clk) y == 0);
endmodule

module power_other_base(input logic clk, input logic [7:0] exponent);
  wire [7:0] y = 8'd3 ** exponent;
  p: assert property (@(posedge clk) y == 0);
endmodule

module power_signed_negative_base(input logic clk, input logic [7:0] exponent);
  wire signed [7:0] y = 2'sb10 ** exponent;
  p: assert property (@(posedge clk) y == 0);
endmodule

module power_truncated_base(input logic clk, input logic [7:0] exponent);
  wire [7:0] y = 1'(3'd2) ** exponent;
  p: assert property (@(posedge clk) y == 0);
endmodule
