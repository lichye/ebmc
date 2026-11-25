module main(input bit clk, reset, request);
  bit blocked = 1;
  bit started = 0;
  bit [1:0] count = 0;
  bit [7:0] detached = 0;

  always @(posedge clk) begin
    blocked <= blocked;
    started <= 1;
    if (reset)
      count <= 0;
    else if (request)
      count <= count + 1;
    if (detached < 8'd3)
      detached <= detached + 1;
  end

`ifndef NO_LOCK
  lock: assume property (@(posedge clk) !blocked || !request);
`endif
  safe: assert property (@(posedge clk) count == 0);
  past_fail: assert property (@(posedge clk) !$past(started));
  reachable: cover property (@(posedge clk) started);
  impossible: cover property (@(posedge clk) count == 1);
  detached_safe: assert property (@(posedge clk) $past(detached) <= 8'd3);
endmodule
