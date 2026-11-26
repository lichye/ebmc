module packed_pattern_target(input logic clk);
  logic [3:0] value;
  assign value = '{0: 1'b1, default: 1'b0};
  p: assert property (@(posedge clk) value == 0);
endmodule
