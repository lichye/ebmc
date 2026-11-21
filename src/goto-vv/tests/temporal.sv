module temporal(input bit clk, d);
  bit [2:0] step = 0;
  bit started = 0, q;
  always_ff @(posedge clk) begin
    step <= step + 1;
    started <= 1;
    q <= d;
  end
  initial_only: assert property (@(posedge clk) started);
  past_ok: assert property (@(posedge clk) started |-> q == $past(d));
  past_bad: assert property (@(posedge clk) started |-> q != $past(d));
  next_ok: assert property (@(posedge clk) step == 0 |=> step == 1);
  next_bad: assert property (@(posedge clk) step == 0 |=> step == 2);
  window_cancel: assert property (@(posedge clk) disable iff (step == 1)
                                   step == 0 |-> ##[1:2] step == 7);
  window_fail: assert property (@(posedge clk) step == 0 |-> ##[1:2] step == 7);
  trigger_live: cover property (@(posedge clk) step == 0);
endmodule
