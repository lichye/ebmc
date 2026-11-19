module main;
  localparam H = $isunknown(2'b1x);
  localparam Z = $isunknown(2'bz0);
  localparam logic [5:0] MIX = {2'b01, 4'bz1x0};
  localparam M = $isunknown(MIX);
  localparam K = $isunknown(4'b1010);
  localparam bit [1:0] BITS = 2'b1x;
  localparam B = $isunknown(BITS);

  has_x: assert final (H);
  has_z: assert final (Z);
  mixed: assert final (M);
  known: assert final (!K);
  two_state: assert final (!B);
endmodule
