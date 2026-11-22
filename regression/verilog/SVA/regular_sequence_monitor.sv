module main(input bit clk);
  bit [3:0] frame = 0;
  always_ff @(posedge clk)
    if (frame != 10)
      frame <= frame + 1;

  wire a = frame == 1;
  wire b = frame == 4;
  wire x = frame == 2;
  wire y = frame == 3;
  wire rst = frame == 7;
  wire never = 1'b0;
  wire c = frame == 2 || frame == 3;

  // The two stars must stay ordered even if either matches no samples.
  ordered: assert property (@(posedge clk) disable iff (rst)
    (a ##1 x[*0:$] ##1 y[*0:$] ##1 b) |-> never);
  reversed: assert property (@(posedge clk) disable iff (rst)
    (a ##1 y[*0:$] ##1 x[*0:$] ##1 b) |-> never);

  // A zero-length trailing repetition still leaves the first sample as
  // a matching endpoint.
  empty_tail_o: assert property (@(posedge clk) disable iff (rst)
    (a ##1 never[*0:$]) |-> never);
  empty_tail_n: assert property (@(posedge clk) disable iff (rst)
    (a ##1 never[*0:$]) |=> never);

  // Both delay lengths match. Only the later obligation fails.
  two_delays: assert property (@(posedge clk) disable iff (rst)
    (a ##1 never[*0:$] ##[1:2] (x || y)) |=> c);

  // Nothing tests a Boolean at the intervening sample, but reset there
  // cancels the attempt.
  reset_gap: assert property (@(posedge clk) disable iff (x)
    (a ##1 never[*0:$] ##2 y) |=> never);

  // The past operand stays at its original tick.
  past_good: assert property (@(posedge clk) disable iff (rst)
    (a ##1 x[*0:$] ##1 y) |=> $past(x, 2));
  past_bad: assert property (@(posedge clk) disable iff (rst)
    (a ##1 x[*0:$] ##1 y) |=> $past(x, 1));
endmodule
