module reduction_xor(input logic clk, input logic [2:0] operand);
  logic [1:0] phase = 0;
  always_ff @(posedge clk)
    if (phase < 3)
      phase <= phase + 2'd1;

  wire [2:0] lanes =
    phase == 0 ? 3'b000 : phase == 1 ? 3'b001 : phase == 2 ? 3'b011 : 3'b111;
  wire [1:0] cfg_i = lanes[2:1];
  wire rst_ni = lanes[0];
  wire parity = ^operand;
  wire concat_parity = ^{cfg_i, rst_ni};
  wire single_parity = ^rst_ni;

  wire [2:0] with_x = 3'b11x;
  wire [2:0] with_z = 3'b10z;
  wire x_parity = ^with_x;
  wire z_parity = ^with_z;
  wire z_driven;
  assign z_driven = z_parity;
  assign z_driven = 1'b1;

  wire x_bit = 1'bx;
  wire z_bit = 1'bz;
  wire x_single = ^x_bit;
  wire z_single = ^z_bit;

  p_parity: assert property (@(posedge clk)
    parity == ((operand[2] != operand[1]) != operand[0]));
  p_concat: assert property (@(posedge clk)
    concat_parity == (phase == 1 || phase == 3));
  p_single: assert property (@(posedge clk) single_parity == rst_ni);
  p_wrong: assert property (@(posedge clk) phase == 2 |-> concat_parity);
  p_x_zero: assert property (@(posedge clk) !x_parity);
  p_x_one: assert property (@(posedge clk) x_parity);
  p_z_zero: assert property (@(posedge clk) !z_parity);
  p_z_one: assert property (@(posedge clk) z_parity);
  p_z_driven: assert property (@(posedge clk) z_driven);
  p_x_single_zero: assert property (@(posedge clk) !x_single);
  p_x_single_one: assert property (@(posedge clk) x_single);
  p_z_single_zero: assert property (@(posedge clk) !z_single);
  p_z_single_one: assert property (@(posedge clk) z_single);
endmodule
