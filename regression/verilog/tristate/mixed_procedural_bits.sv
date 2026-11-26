// Continuous assignments and an always_comb process drive disjoint bits
// of one variable. A gate operand of the continuous drivers can be X (ax) or
// Z through a tristate buffer (az). r0/r1/r2 are the same logic on separate
// signals. Define KNOWN to remove the X and Z.
module main(input logic clk, a, b, c, s, en, sel);
`ifdef KNOWN
  wire ax = a;
  wire az = c;
`else
  wire ax = s ? 1'bx : a;
  wire az;
  assign az = en ? c : 1'bz;
`endif
  logic [2:0] v;
  assign v[0] = v[1] | ~ax;
  always_comb begin
    v[1] = a;
    if (sel) v[1] = b;
  end
  assign v[2] = v[1] & az;

  logic r1;
  wire r0, r2;
  assign r0 = r1 | ~ax;
  always_comb begin
    r1 = a;
    if (sel) r1 = b;
  end
  assign r2 = r1 & az;

  // The procedural bit, also with the X/Z drivers on the other bits.
  p_v1: assert property (@(posedge clk) v[1] == (sel ? b : a));
  p_v1_r1: assert property (@(posedge clk) v[1] == r1);
  // Known values and the dominating operand of the gate.
  p_v0_known: assert property (@(posedge clk) !s |-> v[0] == (v[1] | ~a));
  p_r0_known: assert property (@(posedge clk) !s |-> r0 == (r1 | ~a));
  p_v0_one: assert property (@(posedge clk) v[1] |-> v[0]);
  p_r0_one: assert property (@(posedge clk) r1 |-> r0);
  p_v2_known: assert property (@(posedge clk) en |-> v[2] == (v[1] & c));
  p_r2_known: assert property (@(posedge clk) en |-> r2 == (r1 & c));
  p_v2_zero: assert property (@(posedge clk) !v[1] |-> !v[2]);
  p_r2_zero: assert property (@(posedge clk) !r1 |-> !r2);
  // With X (s, !v[1]) and Z (!en, v[1]) both values are possible
  // (refuted with X/Z; with KNOWN, x_* stay refuted and z_* depend on c).
  x_v0_low: assert property (@(posedge clk) !(s && !v[1] && !v[0]));
  x_v0_high: assert property (@(posedge clk) !(s && !v[1] && v[0]));
  x_r0_low: assert property (@(posedge clk) !(s && !r1 && !r0));
  x_r0_high: assert property (@(posedge clk) !(s && !r1 && r0));
  z_v2_low: assert property (@(posedge clk) !(!en && v[1] && !v[2]));
  z_v2_high: assert property (@(posedge clk) !(!en && v[1] && v[2]));
  z_r2_low: assert property (@(posedge clk) !(!en && r1 && !r2));
  z_r2_high: assert property (@(posedge clk) !(!en && r1 && r2));
  // Wrong values.
  bad_v1: assert property (@(posedge clk) v[1] == (sel ? a : b));
  bad_v0: assert property (@(posedge clk) !s |-> v[0] == (v[1] & ~a));
  bad_v2: assert property (@(posedge clk) en |-> v[2] == (v[1] | c));
endmodule
