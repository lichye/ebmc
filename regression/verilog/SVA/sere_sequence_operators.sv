module main(input bit clk, a);
  wire y = 1'b1, n = 1'b0;
  p0: assert property (@(posedge clk) y throughout (y ##1 y));
  p1: assert property (@(posedge clk) n within (y ##1 y));
  p2: assert property (@(posedge clk) a |-> (a ##[0:2] a));
  p3: assert property (@(posedge clk) y ##[0:2] n);
endmodule
