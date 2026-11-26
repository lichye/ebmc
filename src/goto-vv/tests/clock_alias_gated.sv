module clock_alias_gated(input clk_i, rst_ni, test_enable_i);
  wire parent_clock, child_clock, sync_n;
  wire parent_reset = ~(test_enable_i ? rst_ni : sync_n);
  RstSync sync(.clk_i(clk_i), .rstn_i(rst_ni), .clk_en(1'b1),
               .te(test_enable_i), .clk_o(parent_clock), .rstn_o(sync_n));

  bit [3:0] phase = 0;
  bit child_request = 1;
  always @(posedge clk_i) begin
    phase <= phase + 1'b1;
    child_request <= phase != 6;
  end
  ClockGate child(.clk_i(parent_clock), .enable(child_request),
                  .te(test_enable_i), .clk_o(child_clock));
  wire forwarded, sampled;
  clock_bridge route_clock(child_clock, 1'b0, forwarded);
  assign sampled = forwarded;
`ifdef GATED_ALIAS_DATA
  wire clock_data = ~sampled;
`endif
`ifdef DIRECT_GATE_CLOCK
  `define CLOCK_ALIAS_GATE child_clock
`else
  `define CLOCK_ALIAS_GATE sampled
`endif

  bit q = 0;
  always @(posedge `CLOCK_ALIAS_GATE)
    q <= !q;

  logic [3:0] released;
  bit expected = 0;
  always @(posedge clk_i) begin
    if (!rst_ni)
      released <= 0;
    else
      released <= {released[2:0], 1'b1};
    if (rst_ni && released[3] && child_request)
      expected <= !expected;
  end

`ifndef EVENT_SIMULATION
  p_updates: assert property (@(posedge clk_i) q == expected);
  p_child_closed: assert property (@(posedge `CLOCK_ALIAS_GATE) phase != 7);
  p_wrong: assert property (@(posedge `CLOCK_ALIAS_GATE) phase != 8)
    else $fatal(1, "first open edge at phase 8");
`endif
`undef CLOCK_ALIAS_GATE
endmodule
