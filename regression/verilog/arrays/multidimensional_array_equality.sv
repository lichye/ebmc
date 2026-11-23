module child(input logic [1:0][0:1] p, input logic a [2:1][0:1]);
  wire [1:0][0:1] packed_value = p;
  wire unpacked_value [2:1][0:1] = a;
endmodule

module main(
  input logic [1:0][0:1] p, q,
  input logic a [2:1][0:1],
  input logic b [2:1][0:1]
);
  child u(p, a);

  wire packed_match = p[1] == q[1] && p[0] == q[0];
  wire unpacked_match = a[2][0] == b[2][0] && a[2][1] == b[2][1] &&
                        a[1][0] == b[1][0] && a[1][1] == b[1][1];

  packed_hier_eq: assert final ((u.packed_value == q) == packed_match);
  packed_hier_ne: assert final ((u.packed_value != q) == !packed_match);
  packed_local_eq: assert final ((p == q) == packed_match);
  packed_local_ne: assert final ((p != q) == !packed_match);

  unpacked_hier_eq: assert final ((u.unpacked_value == b) == unpacked_match);
  unpacked_hier_ne: assert final ((u.unpacked_value != b) == !unpacked_match);
  unpacked_local_eq: assert final ((a == b) == unpacked_match);
  unpacked_local_ne: assert final ((a != b) == !unpacked_match);
endmodule
