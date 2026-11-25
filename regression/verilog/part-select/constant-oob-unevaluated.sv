// Constant out-of-range reads in operands that a constant condition never
// selects are not diagnosed (round_to_nearest_even.sv:28 form).
module main #(parameter IN_WIDTH = 6, parameter OUT_WIDTH = 5)
  (input logic [IN_WIDTH-1:0] data_in, output logic a, b, c, d);
  assign a = (IN_WIDTH-2 >= OUT_WIDTH) ? data_in[IN_WIDTH-OUT_WIDTH-2] : 0;
  always_comb begin
    if (IN_WIDTH > 8) b = data_in[8]; else b = 0;
  end
  assign c = (IN_WIDTH > 8) && data_in[8];
  assign d = (IN_WIDTH < 8) || data_in[9:7] != 0;
  p: assert final (a == 0 && b == 0 && c == 0 && d == 1);
endmodule
