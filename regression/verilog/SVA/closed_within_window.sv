module main(input bit clk);
  wire one=1'b1, never=1'b0;
  p0: assert property (@(posedge clk) (never ##1 one) within (one ##1 one ##1 one));
  p1: assert property (@(posedge clk) one within (one ##3 one));
endmodule
