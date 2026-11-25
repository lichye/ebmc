// 1800-2017 27.4: within a loop generate block, the loop index is an
// implicit localparam of type integer: 32 bits, signed.
module main(input logic clk, input logic [2:0] cnt, output logic [3:0] load);
  localparam W = $clog2(4 + 1);
  for (genvar t = 0; t < 4; t++) begin : g
    // the slice is within the 32 bits of t, and reads t's value
    assign load[t] = (cnt == t[W-1:0]);
    p_bits: assert final ($bits(t) == 32 && $bits({t}) == 32);
    p_signed: assert final ((t - 1 < 0) == (t == 0));
    p_slice: assert final (t[2:0] == t && t[31:3] == 0);
    p_load: assert final (load[t] == (cnt == t));
  end
endmodule
