module main(input bit a);
  typedef bit [1:0] unpacked_t [2];
  unpacked_t p;
  always_comb p = unpacked_t'({a,1'bx,1'bz,1'b1});
endmodule
