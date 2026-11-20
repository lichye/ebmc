module main(input clk, d);
  logic q[0:1];
  assign q[0] = d;
  always_ff @(posedge clk) q[1] <= q[0];
  initial begin : checks
    reg_zero: assert(q[1] == 0);
    still_live: assert(q[0] == d);
    input_free: assert(q[0] == 0);
  end
endmodule
