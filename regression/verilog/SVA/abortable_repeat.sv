module main(input bit clk);
  bit [4:0] frame = 0;
  always_ff @(posedge clk)
    if(frame != 16)
      frame <= frame + 1;

  wire start = frame == 1;
  wire keep = frame >= 2 && frame <= 10;
  wire finish = frame == 11;
  wire early_finish = frame == 2;
  wire reset_between = frame == 2;
  wire reset_at_due = frame == 3;
  wire reset_after_due = frame == 4;

  // This match takes the empty branch of the internal repeat.
  zero_overlap: assert property (@(posedge clk) disable iff (reset_at_due)
    (start ##1 keep[*0:$] ##1 early_finish) |-> 1'b0);
  zero_following: assert property (@(posedge clk) disable iff (reset_at_due)
    (start ##1 keep[*0:2] ##1 early_finish) |=> 1'b0);
  late_reset: assert property (@(posedge clk) disable iff (reset_after_due)
    (start ##1 keep[*0:$] ##1 early_finish) |=> 1'b0);

  // No atom is sampled at frame 2 in this match. Reset still aborts it.
  gap_reset: assert property (@(posedge clk) disable iff (reset_between)
    (start ##2 keep[*0:$] ##1 (frame == 3)) |=> 1'b0);

  long_repeat: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*1:$] ##1 finish) |=> 1'b0);

  // Start again after the cancelled first attempt. Both surviving
  // attempts (frames 5 and 6) reach the same endpoint.
  wire start_again = start || frame == 5 || frame == 6;
  restart: assert property (@(posedge clk) disable iff (reset_between)
    (start_again ##1 keep[*0:$] ##1 finish) |-> 1'b0);

  never_finishes: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 1'b1[*0:$] ##1 1'b0) |=> 1'b0);
  long_match: cover property (@(posedge clk) start ##1 keep[*1:$] ##1 finish);
endmodule
