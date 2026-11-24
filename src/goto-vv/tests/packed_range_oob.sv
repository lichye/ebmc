module packed_range_oob(input logic clk, input logic [2:5][7:0] lanes);
  wire [15:0] selected = lanes[5 +: 2];
  p: assert property (@(posedge clk) selected != 0);
endmodule
