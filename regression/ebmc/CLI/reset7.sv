// --reset with an active-low reset: the reset holds in the initial
// state, and not in any later state. The reset signal is an input.
// All engines must agree.
module main(input clk, input rst_n);

  reg seen = 0;
  reg [3:0] c;

  always @(posedge clk) begin
    seen <= 1;
    if (!rst_n) c <= 0; else c <= c + 1;
  end

  // holds: the reset is only in the initial state
  p_once: assert property (@(posedge clk) seen -> rst_n);

  // fails: c counts 0, 1, 2, 3 after the reset
  p_count: assert property (@(posedge clk) seen -> c != 3);

endmodule
