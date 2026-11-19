module child #(parameter type T=logic)(input T a, output T y);
  assign y=~a;
endmodule
module main(input logic [3:0] a);
  typedef logic [3:0] word_t;
  wire [3:0] y, z;
  child #(.T(word_t)) u(.a(a),.y(y));
  child #(word_t) v(.a(a),.y(z));
  p0: assert property (y==~a && z==~a);
endmodule
