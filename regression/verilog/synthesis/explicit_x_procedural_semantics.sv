// Spec 29 semantics of an explicit x committed by an always construct.
// p_* must be PROVED, r_* must be REFUTED (x is unconstrained), k_* record
// the remaining known behaviour.
module main(input clk, input logic en, input logic [1:0] t);
  // one write, several reads: consistent
  logic r = 0; logic q1 = 0, q2 = 0;
  always_ff @(posedge clk) r <= 1'bx;
  always_ff @(posedge clk) begin q1 <= r; q2 <= r; end
  p_same_read: assert property (@(posedge clk) q1 == q2);
  r_unknown:   assert property (@(posedge clk) r == 0);
  // write enable: hold without a write, a new value on each write
  logic h = 0;
  always_ff @(posedge clk) if (en) h <= 1'bx;
  p_hold:      assert property (@(posedge clk) !$past(en) |-> h == $past(h));
  r_new_value: assert property (@(posedge clk) $past(en) |-> h == $past(h));
  // partial write: known neighbours keep their value
  logic [3:0] v = 0;
  always_ff @(posedge clk) begin v[3:2] <= 2'bxx; v[1:0] <= t; end
  p_known_bits: assert property (@(posedge clk) ##1 v[1:0] == $past(t));
  r_x_bits:     assert property (@(posedge clk) v[3:2] == 0);
  // non-blocking: the old value is read in the same process
  logic o = 0, s = 0;
  always_ff @(posedge clk) begin o <= 1'bx; s <= o; end
  p_nba_old:   assert property (@(posedge clk) ##1 s == $past(o));
  // packed struct: x only on the four-state field
  typedef struct packed { logic a; bit b; logic [1:0] c; } st_t;
  st_t m = '0;
  always_ff @(posedge clk) m <= 4'bxxxx;
  st_t m2 = '0;
  always_ff @(posedge clk) m2 <= 'x;   // unbased unsized literal into a struct
  k_struct_fill: assert property (@(posedge clk) m2.c[1] == 0);
  logic [3:0] w = 0;
  always_ff @(posedge clk) w <= 'x;
  r_vector_fill: assert property (@(posedge clk) w[3] == 0);
  p_two_state_field: assert property (@(posedge clk) m.b == 0);
  r_four_state_a:    assert property (@(posedge clk) m.a == 0);
  r_four_state_c:    assert property (@(posedge clk) m.c == 0);
  // reads inside the process still see x/z
  logic c, d;
  always_comb begin c = 1'bx; d = (c === 1'bx); end
  k_inprocess_x: assert property (@(posedge clk) d == 1);
  r_comb_value:  assert property (@(posedge clk) c == 0);
  // two-state targets and explicit conversions stay 0
  bit b2 = 0;
  always_ff @(posedge clk) b2 <= 1'bx;
  p_two_state_var: assert property (@(posedge clk) b2 == 0);
  // x from an operator (not a literal) is outside this change
  logic e = 0;
  always_ff @(posedge clk) e <= t[0] & 1'bx;
  k_operator_x: assert property (@(posedge clk) e == 0);
endmodule
