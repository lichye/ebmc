module compound_target(input logic clk);
  logic [3:0] value = 4'b0;
  int index = 0;
  initial value[index++] += 1'b1;
  p: assert property (@(posedge clk) value != 0);
endmodule
