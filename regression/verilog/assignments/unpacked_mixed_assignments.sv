module main(input bit clk,a,b);
  logic x[0:1];
  assign x[0] = a;
  always_ff @(posedge clk) x[1] <= b;
  assert property (x[0] == a);
endmodule
