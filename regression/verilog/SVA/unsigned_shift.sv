module main(input bit signed [3:0] s);
  p0: assert property ((s >>> 3) <= 4'b0001);
  p1: assert property (!((s >>> 3) <= 4'b0001));
endmodule
