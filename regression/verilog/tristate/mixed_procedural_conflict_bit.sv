// A second continuous assignment and the process both
// drive bit 1, next to an X gate driver on bit 0.
module main(input logic clk, a, b, s);
  wire ax = s ? 1'bx : a;
  logic [2:0] v;
  assign v[0] = v[2] | ~ax;
  assign v[1] = a;
  always_comb begin
    v[2] = b;
    v[1] = b;
  end
  p: assert property (@(posedge clk) v[2] == b);
endmodule
