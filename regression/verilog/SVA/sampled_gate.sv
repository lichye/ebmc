module main(input bit clk,a,g);
  p0: assert property (@(posedge clk) $past(a,1,g)==a);
endmodule
