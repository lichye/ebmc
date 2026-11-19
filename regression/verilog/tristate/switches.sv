module main(input a, e, n, p);
  wire b0, b1, i0, i1, mn, mp, rn, rp, c, rc;
  bufif0 (b0, a, e);
  bufif1 (b1, a, e);
  notif0 (i0, a, e);
  notif1 (i1, a, e);
  nmos (mn, a, e);
  pmos (mp, a, e);
  rnmos (rn, a, e);
  rpmos (rp, a, e);
  cmos (c, a, n, p);
  rcmos (rc, a, n, p);
  high: assert property (e |-> b1 == a && i1 == !a && mn == a && rn == a);
  low: assert property (!e |-> b0 == a && i0 == !a && mp == a && rp == a);
  transmission: assert property ((n || !p) |-> c == a && rc == a);

  wire [2:0] broadcast;
  bufif1 bank[2:0](broadcast, a, e);
  broadcast_inputs: assert property (e |-> broadcast == {3{a}});
  tri1 shared;
  nmos bank_out[1:0](shared, {a, !a}, e);
  broadcast_output: assert property (!e |-> shared);
  array_conflict: assert property (e |-> shared);

  tri1 disabled;
  bufif1 (disabled, 1'b0, 1'b0);
  release_bus: assert property (disabled);

  // A MOS with unknown control releases a z data terminal. An enabled
  // co-driver can distinguish release from an unknown drive.
  wire zdata = 1'bz;
  tri1 mos_z;
  nmos (mos_z, zdata, 1'bx);
  pmos (mos_z, 1'bz, 1'bz);
  released_mos: assert property (mos_z);
  tri1 unknown_gate;
  bufif1 (unknown_gate, 1'bz, 1'bx);
  unknown_can_be_zero: assert property (unknown_gate);
  unknown_can_be_one: assert property (!unknown_gate);
endmodule
