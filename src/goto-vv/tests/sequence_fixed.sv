module sequence_fixed(input logic clk);
  localparam int EIGHT = 4 + 4;
  bit [3:0] frame = 0;
  always @(posedge clk)
    if (frame != 14) frame <= frame + 1'b1;

  wire start = frame == 1;
  wire keep = frame >= 2 && frame <= 10;
  wire marker = frame >= 1 && frame <= 9;
  wire finish = frame == 9 || frame == 10 || frame == 11;

  zero_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*0] ##1 (frame == 2)) |=> (frame != 3));
  seven_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 (keep[*7]) ##1 finish) |=> (frame != 10));
  property eight_samples;
    @(posedge clk) disable iff (frame == 0)
    (start ##1 (keep && $stable(marker))[*EIGHT] ##1 finish) |=> (frame != 11);
  endproperty
  eight_bad: assert property (eight_samples);
  nine_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*9] ##1 finish) |=> (frame != 12));

  reset_in_repeat: assert property (@(posedge clk) disable iff (frame == 5)
    (start ##1 keep[*8] ##1 finish) |=> (frame != 11));
  reset_at_due: assert property (@(posedge clk) disable iff (frame == 11)
    (start ##1 keep[*8] ##1 finish) |=> (frame != 11));
  reset_after_due: assert property (@(posedge clk) disable iff (frame == 12)
    (start ##1 keep[*8] ##1 finish) |=> (frame != 11));
  no_shortcut: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 (keep && frame != 5)[*8] ##1 finish) |=> (frame != 11));
  eight_good: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*8] ##1 finish) |=> (frame == 11));
endmodule
