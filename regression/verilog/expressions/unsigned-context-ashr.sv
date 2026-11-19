module m(input [3:0] a);
  p: assert final (a == 4'b1000 -> (($signed(a) >>> 1) == 4'b0100));
endmodule
