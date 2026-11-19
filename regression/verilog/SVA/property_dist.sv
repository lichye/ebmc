module main(input bit [1:0] x);
  p0: assert property (x < 3 |-> x dist {0 := 2, [1:2] :/ 3});
  p1: assert property (x dist {0 := 0, 1 := 1} |-> x == 1);
endmodule
