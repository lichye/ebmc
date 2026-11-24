module sequence_star (input logic clk);
  bit [3:0] frame = 0;
  always @(posedge clk)
    if (frame != 9) frame <= frame + 1'b1;

  wire start = frame == 1;
  wire keep = frame >= 2 && frame <= 4;
  wire keep_once = frame == 2;
  wire finish_zero = frame == 2;
  wire finish_one = frame == 4;
  wire finish_many = frame == 5;
  wire finish_any = frame == 2 || frame == 3 || frame == 5;

  zero_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*0:$] ##1 finish_zero) |-> (frame != 2));
  one_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep_once[*0:$] ##2 finish_one) |-> (frame != 4));
  many_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 (frame >= 2 && frame <= 4)[*0:$] ##1 finish_many) |=> (frame != 6));

  property endpoints;
    @(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*0:$] ##1 finish_any) |=> (frame == 3 || frame == 4 || frame == 6);
  endproperty
  all_ends_ok: assert property (endpoints);
  all_ends_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##1 keep[*0:$] ##1 finish_any) |=> (frame == 3 || frame == 4));

  wire start_twice = frame == 1 || frame == 2;
  wire keep_late = frame >= 3 && frame <= 4;
  restart_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (start_twice ##1 keep_late[*0:$] ##1 finish_many) |=> (frame != 6));

  gap_reset: assert property (@(posedge clk) disable iff (frame == 2)
    (start ##2 keep[*0:$] ##1 (frame == 3)) |=> (frame != 4));
  gap_live: assert property (@(posedge clk) disable iff (frame == 0)
    (start ##2 keep[*0:$] ##1 (frame == 3)) |=> (frame != 4));
  due_reset: assert property (@(posedge clk) disable iff (frame == 3)
    (start ##1 keep[*0:$] ##1 finish_zero) |=> (frame != 3));
  later_reset_bad: assert property (@(posedge clk) disable iff (frame == 4)
    (start ##1 keep[*0:$] ##1 finish_zero) |=> (frame != 3));
endmodule
