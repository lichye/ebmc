module main(input bit clk, input bit a);
  property invalid;
    @(posedge clk) (1, a = 0) |-> 1;
  endproperty
  p: assert property (invalid);
endmodule
