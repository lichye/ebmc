module main(input clk_a, clk_b, a, b, d);
  reg mem[0:1];
  always @(posedge clk_a) mem[a] <= d;
  always @(posedge clk_b) mem[b] <= !d;
endmodule
