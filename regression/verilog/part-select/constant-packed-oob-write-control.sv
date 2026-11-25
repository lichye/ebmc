module control(input logic clk, input logic [3:0] d, input logic [7:0] idx);
  logic [3:0] q = 0;
  always_ff @(posedge clk) q[5:2] <= 4'b0101;
  p_write: assert property (@(posedge clk) 1'b1 |=> q == 4'b0100);
  p_dynamic: assert property (@(posedge clk) (idx == 1) |-> d[idx +: 2] == d[2:1]);
endmodule
