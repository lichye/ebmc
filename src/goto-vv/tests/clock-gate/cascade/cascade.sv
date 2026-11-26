module reset_source(
  input clock,
  input reset,
  input [1:0] next_reset,
  output csr_reset,
  output debug_reset
);
  reg [1:0] csr, debug;
  always @(posedge clock or posedge reset) begin
    if (reset) begin
      csr <= 2'b01;
      debug <= 2'b00;
    end else begin
      csr <= {1'b0, next_reset[0]};
      debug <= {next_reset[1], 1'b0};
    end
  end
  assign csr_reset = csr[0];
  assign debug_reset = debug[1];
endmodule

module cascade_consumer(input clock, reset, data, output reg q);
  always @(posedge clock or posedge reset) begin
    if (reset) q <= 1'b0;
    else q <= data;
  end
endmodule

module cascade(
  input clk_i, rst_ni, test_enable_i, enable_i, root_enable,
  input data,
  input [1:0] next_reset,
  output q, csr_reset, debug_reset,
  output reg root_q
);
  wire parent_clock, child_clock, independent_clock;
  wire sync_n;
  wire parent_reset = ~(test_enable_i ? rst_ni : sync_n);
  RstSync sync(.clk_i(clk_i), .rstn_i(rst_ni), .clk_en(1'b1),
               .te(test_enable_i), .clk_o(parent_clock), .rstn_o(sync_n));
  reset_source source(.clock(parent_clock), .reset(parent_reset),
                      .next_reset(next_reset), .csr_reset(csr_reset),
                      .debug_reset(debug_reset));
  reg child_request = 1'b0, root_request = 1'b0;
  always @(posedge clk_i) begin
    child_request <= enable_i;
    root_request <= root_enable;
  end
  ClockGate child(.clk_i(parent_clock), .enable(child_request), .te(test_enable_i), .clk_o(child_clock));
  ClockGate independent(.clk_i(clk_i), .enable(root_request), .te(test_enable_i), .clk_o(independent_clock));
  cascade_consumer core(.clock(child_clock), .reset(test_enable_i ? ~rst_ni : csr_reset | debug_reset),
                        .data(data), .q(q));

  initial root_q = 1'b0;
  always @(posedge independent_clock) root_q <= data;

`ifndef EVENT_COMPARE
  // Begin a history only after a visible valid sample.
  reg have_previous;
  initial have_previous = 1'b0;
  always @(posedge clk_i) have_previous <= 1'b1;
  p_assertion: assert property (@(posedge clk_i) (csr_reset || debug_reset) |-> !q);
  p_independent: assert property (@(posedge clk_i)
    have_previous |-> (root_q == ($past(root_request) ? $past(data) : $past(root_q))));
  p_child_hold: assert property (@(posedge clk_i)
    (have_previous && !$past(child_request) && !$past(csr_reset || debug_reset) &&
      !csr_reset && !debug_reset) |-> (q == $past(q)));
  p_release: cover property (@(posedge clk_i)
    (!rst_ni && csr_reset) ##1 !rst_ni ##1 rst_ni[*6] ##1 (q && !csr_reset && !debug_reset));
  p_pulse: cover property (@(posedge clk_i)
    (q && !csr_reset && !debug_reset && !child_request) ##1 (!q && debug_reset && !child_request)
        ##1 (!q && !csr_reset && !debug_reset && !child_request));
  p_bad: assert property (@(posedge clk_i) !q);
`endif
endmodule
