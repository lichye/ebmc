module main(input bit clk, input bit [1:0] a);
  bit [3:0] storage;
  p: assert property (@(posedge clk) (1, storage[1:0] = a) |-> 1);
endmodule
