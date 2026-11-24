module packed_select_oob(input logic clk, input logic [1:0][7:0] packed_input);
  wire [7:0] selected = packed_input[2];
  p: assert property (@(posedge clk) selected != 0);
endmodule
