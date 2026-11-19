module main(input bit clk, input bit [1:0] a, b);
  property assemble;
    bit [3:0] v;
    @(posedge clk) (1, v[1:0] = a, v[3:2] = b) |-> v == {b, a};
  endproperty
  p: assert property (assemble);
endmodule
