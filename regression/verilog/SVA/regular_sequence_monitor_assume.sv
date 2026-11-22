module main(input bit clk, alarm);
  bit [3:0] frame = 0;
  always_ff @(posedge clk) if (frame != 8) frame <= frame + 1;
  wire rst = frame == 0;
  wire a = frame == 1;
  wire keep = frame == 2;
  wire b = frame == 3;
  wire c = frame != 4;
  deadline: assume property (@(posedge clk) (frame == 1) |-> ##5 alarm);
`ifdef LOCKED
  conflict: assume property (@(posedge clk) !alarm);
`else
  quiet: assume property (@(posedge clk) frame != 6 |-> !alarm);
`endif
  p: assert property (@(posedge clk) disable iff (rst)
    (a ##1 keep[*0:$] ##1 b) |=> c);
endmodule
