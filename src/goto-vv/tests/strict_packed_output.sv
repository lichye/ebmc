module strict_packed_output_leaf(input logic d, output logic ready);
  assign ready = !d;
endmodule

module strict_packed_output(input logic clk, d);
  wire [0:0] ready;
  for (genvar i = 0; i < 1; ++i) begin : lane
    strict_packed_output_leaf u(.d(d), .ready(ready[i]));
  end
  wire [3:3] ready_nz;
  strict_packed_output_leaf u_nz(.d(d), .ready(ready_nz[3]));
  good: assert property (@(posedge clk) ready[0] == !d);
  wrong: assert property (@(posedge clk) ready[0] == d);
  nonzero_good: assert property (@(posedge clk) ready_nz == !d);
  nonzero_wrong: assert property (@(posedge clk) ready_nz == d);
endmodule
