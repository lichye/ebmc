// The process also writes bit 0, which a continuous
// assignment drives. Define KNOWN for the operand without X.
module main(input logic clk, a, b, s);
`ifdef KNOWN
  wire ax = a;
`else
  wire ax = s ? 1'bx : a;
`endif
  logic [1:0] v;
  assign v[0] = v[1] | ~ax;
  always_comb v[1:0] = {b, a};
  p: assert property (@(posedge clk) v[1] == b);
endmodule
