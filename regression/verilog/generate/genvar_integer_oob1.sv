// A genvar is a 32-bit integer (1800-2017 27.4): t[33:30] reads two
// in-range bits (0) and two out-of-range bits (x).
module main(input logic clk);
  for (genvar t = 0; t < 2; t++) begin : g
    p: assert final (t[33:30] === 4'bxx00 && t[2:0] == t);
  end
endmodule
