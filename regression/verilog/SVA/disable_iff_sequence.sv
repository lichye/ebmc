module top(input bit clk);
  bit [3:0] tick = 0;
  always @(posedge clk)
    if (tick < 10) tick <= tick + 1;

  wire start = tick == 1;
  wire matched = tick == 2;
  wire c_overlap = tick == 2;
  wire c_next = tick == 3;
  wire at_start = tick == 1;
  wire at_match = tick == 2;
  wire at_due = tick == 3;
  wire after_due = tick == 4;

  overlap_ok: assert property (@(posedge clk) disable iff (after_due)
    (start ##1 matched) |-> c_overlap);
  next_ok: assert property (@(posedge clk) disable iff (after_due)
    (start ##1 matched) |=> c_next);
  overlap_bad: assert property (@(posedge clk) disable iff (at_due)
    (start ##1 matched) |-> c_next);
  next_bad: assert property (@(posedge clk) disable iff (after_due)
    (start ##1 matched) |=> c_overlap);

  overlap_start: assert property (@(posedge clk) disable iff (at_start)
    (start ##1 matched) |-> 1'b0);
  overlap_match: assert property (@(posedge clk) disable iff (at_match)
    (start ##1 matched) |-> 1'b0);
  next_start: assert property (@(posedge clk) disable iff (at_start)
    (start ##1 matched) |=> 1'b0);
  next_match: assert property (@(posedge clk) disable iff (at_match)
    (start ##1 matched) |=> 1'b0);
  next_due: assert property (@(posedge clk) disable iff (at_due)
    (start ##1 matched) |=> 1'b0);

  wire recurring = start || tick == 5;
  wire match_recurring = matched || tick == 6;
  restart_bad: assert property (@(posedge clk) disable iff (at_match)
    (recurring ##1 match_recurring) |=> 1'b0);
  restart_ok: assert property (@(posedge clk) disable iff (at_match)
    (recurring ##1 match_recurring) |=> tick == 7);

  wire flexible = matched || tick == 3;
  multi_bad: assert property (@(posedge clk) disable iff (tick == 5)
    (start ##[1:2] flexible) |=> c_next);
  multi_abort_late: assert property (@(posedge clk) disable iff (after_due)
    (start ##[1:2] flexible) |=> c_next);
  multi_fail_early: assert property (@(posedge clk) disable iff (after_due)
    (start ##[1:2] flexible) |=> tick == 4);
  antecedent: cover property (@(posedge clk) start ##1 matched);
  restarted: cover property (@(posedge clk) (tick == 5) ##1 match_recurring);
endmodule
