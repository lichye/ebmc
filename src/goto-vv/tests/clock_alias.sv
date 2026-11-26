module transparent_cell(input clk, request, output passed);
  for (genvar i = 0; i < 1; ++i) begin : route
    wire mid;
    assign mid = clk;
    assign passed = mid;
  end
endmodule

module clock_bridge(input source, request, output clock_out);
  wire local_clock;
  transparent_cell cell_i(.clk(source), .request(request), .passed(local_clock));
  assign clock_out = local_clock;
endmodule

module clock_alias(input clk);
  bit [2:0] phase = 0;
  bit first = 0, second = 0;
  wire request = phase == 6;
  wire forwarded, alias_clk;
  // This second occurrence has the same specialization and an unrelated actual.
  clock_bridge unused_path(1'b0, request, );
  clock_bridge live_path(clk, request, forwarded);
  assign alias_clk = forwarded;

  always @(posedge clk) begin
    if (phase != 7) phase <= phase + 3'd1;
    first <= phase == 0 || phase == 3;
  end
`ifdef DIRECT_CLOCK
  `define CLOCK_ALIAS_SAMPLE clk
`else
  `define CLOCK_ALIAS_SAMPLE alias_clk
`endif
  always @(posedge `CLOCK_ALIAS_SAMPLE)
    second <= first;

`ifndef EVENT_SIMULATION
  p_pipeline: assert property (@(posedge `CLOCK_ALIAS_SAMPLE)
    first == (phase == 1 || phase == 4) &&
    second == (phase == 2 || phase == 5));
  p_request_closed: assert property (@(posedge clk)
    (phase == 2 || phase == 5) |-> !request && second);
  p_wrong: assert property (@(posedge clk) phase == 2 |-> !second);
`endif
`undef CLOCK_ALIAS_SAMPLE
endmodule
