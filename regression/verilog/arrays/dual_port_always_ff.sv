module main(input clk, a, b, d);
  reg mem[0:1];
  always_ff @(posedge clk) mem[a] <= d;
  always @(posedge clk) mem[b] <= !d;
endmodule
