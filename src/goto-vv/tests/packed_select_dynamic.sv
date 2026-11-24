module packed_select_dynamic(input logic clk, input logic [1:0] index, input logic [1:0][7:0] packed_input);
  wire [7:0] selected = packed_input[index];
  p: assert property (@(posedge clk) selected != 0);
endmodule
