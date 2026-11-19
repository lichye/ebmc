module main(input a, b, e1, e2, step_clock);
  tri y;
  assign y = e1 ? a : 1'bz;
  assign y = e2 ? b : 1'bz;
  left: assert property ((e1 && !e2) |-> y == a);
  right: assert property ((!e1 && e2) |-> y == b);
  agree: assert property ((e1 && e2 && a == b) |-> y == a);
  conflict_zero: assert property ((e1 && e2 && a != b) |-> !y);
  conflict_one: assert property ((e1 && e2 && a != b) |-> y);
  consistent: assert property ((y ^ y) == 0);

  wire floating = 1'bz;
  wire alias_net = floating;
  shared_read: assert property (floating == alias_net);
  can_read_zero: assert property (floating);
  can_read_one: assert property (!floating);
  reg last_floating, last_y, last_conflict, past_valid = 0;
  always @(posedge step_clock) begin
    past_valid <= 1;
    last_floating <= floating;
    last_y <= y;
    last_conflict <= e1 && e2 && a != b;
  end
  independent_steps: assert property (past_valid |-> floating == last_floating);
  independent_conflicts: assert property (
    (past_valid && last_conflict && e1 && e2 && a != b) |-> y == last_y);
endmodule
