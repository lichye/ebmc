module main(input bit clk,a,b);
  logic [3:0] q;
  always_ff @(posedge clk) q[2:1] <= {a,b};
  always_ff @(posedge clk) q[1:0] <= {b,a};
  assert property (q[1] == a);
endmodule
