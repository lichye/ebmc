module main(input bit clk, value);
  wire sampled = $past(value, 2);
  // The counter rules out a loop within the default lasso horizon.
  logic [7:0] counter = 0;
  always @(posedge clk) counter <= counter + 1;

  goal: assert property (@(posedge clk) !sampled);
endmodule
