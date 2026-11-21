// At K=3 a spurious launch@0 can finish at 1. A genuine launch@3 would
// finish at 4, outside the native property horizon; only K=4 admits it.
module future_match(input bit clk, req, alarm);
  bit [2:0] tick = 0;
  always_ff @(posedge clk) if (tick < 6) tick <= tick + 1'b1;
  wire early = tick == 0 && req;
  wire trigger_event = early || tick == 3;
  wire condition = tick != 1 && tick != 4;
  a1: assume property (@(posedge clk) early |-> ##4 alarm);
  a2: assume property (@(posedge clk) !alarm);
  p: assert property (@(posedge clk) trigger_event |=> condition);
  hit: cover property (@(posedge clk) trigger_event ##1 !condition);
endmodule
