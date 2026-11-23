module w10(input logic clk, input logic rst, input logic a, input logic b, input logic c0, input logic c1, input logic c2, input logic c3, input logic d);
  p: assert property (@(posedge clk) disable iff (rst) $past(rst) && a ##1 b[*0:$] ##1 c0 ##1 c1 ##1 c2 ##1 c3 ##1 c0 ##1 c1 ##1 c2 ##1 c3 ##1 c0 ##1 c1 |-> d);
endmodule
