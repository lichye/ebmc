// A signed value with X bits in signed and unsigned arithmetic. The
// X comes from a conditional with an X selector, so equal bits of the arms
// stay known (1800-2017 11.4.11). q* are the same expressions on a
// two-state copy (re); the e* properties spell out the extension and
// truncation rules (11.8.2).
module main(input logic clk, s, input logic [3:0] a, b, c,
                      input logic [7:0] u);
  wire xsel = s ? 1'bx : a[0];
  logic signed [8:0] e0, e1, se, re, sf;
  assign e0 = {{5{a[3]}}, a};
  assign e1 = {{5{b[3]}}, b};
  assign se = xsel ? e1 : e0;        // X bits where e0, e1 differ when s
  assign re = a[0] ? e1 : e0;        // the two-state value when !s
  assign sf = {{5{c[3]}}, c};
  logic [8:0] uu;
  assign uu = {1'b0, u};

  logic [8:0] r1, q1, r2, q2, r4, q4, r5, q5;
  logic signed [8:0] r3, q3, r6, q6, r10, q10;
  logic [10:0] r7, q7;
  logic signed [10:0] r8, q8;
  logic [5:0] r9, q9;
  assign r1 = se + uu;               assign q1 = re + uu;
  assign r2 = se - uu;               assign q2 = re - uu;
  assign r3 = se + sf;               assign q3 = re + sf;
  assign r4 = -se + uu;              assign q4 = -re + uu;
  assign r5 = unsigned'(se) + uu;    assign q5 = unsigned'(re) + uu;
  assign r6 = se - signed'(uu);      assign q6 = re - signed'(uu);
  assign r7 = se + uu;               assign q7 = re + uu;
  assign r8 = se + sf;               assign q8 = re + sf;
  assign r9 = se - uu;               assign q9 = re - uu;
  assign r10 = signed'(se + sf - uu); assign q10 = signed'(re + sf - uu);

  // Known operands: the four-state path equals the two-state path.
  p_add: assert property (@(posedge clk) !s |-> r1 == q1);
  p_sub: assert property (@(posedge clk) !s |-> r2 == q2);
  p_add_signed: assert property (@(posedge clk) !s |-> r3 == q3);
  p_neg: assert property (@(posedge clk) !s |-> r4 == q4);
  p_cast_unsigned: assert property (@(posedge clk) !s |-> r5 == q5);
  p_cast_signed: assert property (@(posedge clk) !s |-> r6 == q6);
  p_wide_unsigned: assert property (@(posedge clk) !s |-> r7 == q7);
  p_wide_signed: assert property (@(posedge clk) !s |-> r8 == q8);
  p_truncate: assert property (@(posedge clk) !s |-> r9 == q9);
  p_fpu_shape: assert property (@(posedge clk) !s |-> r10 == q10);
  // An X selector with equal arms gives a known value.
  p_x_agree: assert property (@(posedge clk) s && e0 == e1 |-> r1 == e0 + uu && r8 == e0 + sf);
  // The rules, written out: an unsigned operand makes the expression
  // unsigned (zero extension), all-signed operands sign-extend.
  e_wide_unsigned: assert property (@(posedge clk) !s |-> r7 == {2'b0, re} + {2'b0, uu});
  e_wide_signed: assert property (@(posedge clk) !s |-> r8 == {{2{re[8]}}, re} + {{2{sf[8]}}, sf});
  e_truncate: assert property (@(posedge clk) !s |-> r9 == re[5:0] - uu[5:0]);
  e_neg: assert property (@(posedge clk) !s |-> r4 == uu - re);
  // Unknown operand: every value is possible.
  x_add_zero: assert property (@(posedge clk) !(s && e0 != e1 && r1 == 0));
  x_add_nonzero: assert property (@(posedge clk) !(s && e0 != e1 && r1 != 0));
  x_sub_zero: assert property (@(posedge clk) !(s && e0 != e1 && r2 == 0));
  x_wide_top: assert property (@(posedge clk) !(s && e0 != e1 && r7[10]));
  // Wrong values.
  bad_add: assert property (@(posedge clk) !s |-> r1 == q1 + 1);
  bad_wide_sign: assert property (@(posedge clk) !s |-> r7 == {{2{re[8]}}, re} + {2'b0, uu});
  bad_wide_zero: assert property (@(posedge clk) !s |-> r8 == {2'b0, re} + {2'b0, sf});
  bad_neg: assert property (@(posedge clk) !s |-> r4 == re + uu);
endmodule
