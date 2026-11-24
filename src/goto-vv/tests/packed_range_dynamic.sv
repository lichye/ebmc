module packed_range_dynamic(input logic clk, input logic [1:0] index, input logic [3:0][7:0] lanes);
  wire [15:0] selected = lanes[index +: 2];
  p: assert property (@(posedge clk) selected != 0);
endmodule
