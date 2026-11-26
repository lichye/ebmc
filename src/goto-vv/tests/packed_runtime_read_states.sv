// Spec 31 E9: four- and two-state elements under runtime reads.
module packed_runtime_read_states(input logic clk, input logic [1:0] vc);
  // four-state element with x/z in a constant
  wire logic [1:0][3:0] xs = {4'b1x0z, 4'b0101};
  wire logic [3:0] r_x = xs[vc];
  p_x_known: assert property (@(posedge clk) vc == 1 |-> r_x[3] == 1 && r_x[1] == 0);
  r_x_bit0: assert property (@(posedge clk) vc == 1 |-> r_x[2] == 0);
  r_x_bit1: assert property (@(posedge clk) vc == 1 |-> r_x[2] == 1);
  p_x_in0: assert property (@(posedge clk) vc == 0 |-> r_x == 4'b0101);
  // out of range on a four-state element: unconstrained, no wrap
  r_x_oob0: assert property (@(posedge clk) vc == 2 |-> r_x == 4'b0101);
  r_x_oob1: assert property (@(posedge clk) vc == 2 |-> r_x != 4'b0101);
  // two-state element: 0 out of range (1800-2017 7.4.6)
  bit [1:0][3:0] bs;
  bit [3:0] r_b;
  assign bs = 8'h5a;
  assign r_b = bs[vc];
  p_b_in0: assert property (@(posedge clk) vc == 0 |-> r_b == 4'ha);
  p_b_in1: assert property (@(posedge clk) vc == 1 |-> r_b == 4'h5);
  p_b_oob: assert property (@(posedge clk) vc[1] |-> r_b == 0);
  // explicit slice references on the flat vectors
  wire logic [7:0] xflat = xs;
  wire logic [3:0] r_xs = xflat[vc * 4 +: 4];
  r_xs_oob0: assert property (@(posedge clk) vc == 2 |-> r_xs == 4'b0101);
  p_xs_oob1: assert property (@(posedge clk) vc == 2 |-> r_xs != 4'b0101);
  bit [7:0] bflat;
  bit [3:0] r_bs;
  assign bflat = bs;
  assign r_bs = bflat[vc * 4 +: 4];
  p_bs_oob: assert property (@(posedge clk) vc[1] |-> r_bs == 0);
  // the same reads directly in a property
  r_x_prop: assert property (@(posedge clk) vc == 2 |-> xs[vc] == 4'b0101);
  p_b_prop: assert property (@(posedge clk) vc[1] |-> bs[vc] == 0);
endmodule
