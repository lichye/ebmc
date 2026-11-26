`ifdef EXPLICIT_REFERENCE
  `define NOR(v) !(|v)
  `define NAND(v) !(&v)
  `define XNOR(v) !(^v)
  `define XNOR_ALT(v) !(^v)
`else
  `define NOR(v) ~|v
  `define NAND(v) ~&v
  `define XNOR(v) ~^v
  `define XNOR_ALT(v) ^~v
`endif

module reduction_inverted(input logic clk, input logic [2:0] operand);
  logic [1:0] phase = 0;
  always_ff @(posedge clk)
    if (phase < 3)
      phase <= phase + 2'd1;

  wire [2:0] lanes =
    phase == 0 ? 3'b000 : phase == 1 ? 3'b001 : phase == 2 ? 3'b011 : 3'b111;
  wire scalar = lanes[0];
  wire nor_lanes = `NOR(lanes);
  wire nand_lanes = `NAND(lanes);
  wire xnor_lanes = `XNOR(lanes);
  wire alt_lanes = `XNOR_ALT(lanes);
  wire [7:0] wide_nor = `NOR(lanes);
  wire [7:0] wide_nand = `NAND(lanes);
  wire [7:0] wide_xnor = `XNOR(lanes);

  wire x_bit = 1'bx;
  wire z_bit = 1'bz;
  wire masked_nor = `NOR({1'b1, x_bit, z_bit});
  wire masked_nand = `NAND({1'b0, z_bit, x_bit});
  wire masked_literal_nor = `NOR(3'b1xz);
  wire masked_literal_nand = `NAND(3'b0zx);
  wire unknown_nor = `NOR({1'b0, x_bit, z_bit});
  wire unknown_nand = `NAND({1'b1, z_bit, x_bit});
  wire unknown_xnor = `XNOR({1'b1, x_bit, z_bit});
  wire unknown_scalar = `XNOR_ALT(z_bit);
  wire unknown_driven;
  assign unknown_driven = unknown_nor;
  assign unknown_driven = 1'b1;

`ifndef EVENT_SIMULATION
  p_constants: assert property (@(posedge clk)
    `NOR(3'b000) && !(`NOR(3'b101)) &&
    `NAND(3'b001) && !(`NAND(3'b111)) &&
    `XNOR(3'b101) && !(`XNOR_ALT(3'b100)));
  p_runtime: assert property (@(posedge clk)
    `NOR(operand) == (operand == 3'b000) &&
    `NAND(operand) == (operand != 3'b111) &&
    `XNOR(operand) == ((operand[2] == operand[1]) != operand[0]) &&
    `XNOR_ALT(operand) == `XNOR(operand));
  p_patterns: assert property (@(posedge clk)
    nor_lanes == (phase == 0) && nand_lanes == (phase != 3) &&
    xnor_lanes == (phase == 0 || phase == 2) && alt_lanes == xnor_lanes);
  p_scalar: assert property (@(posedge clk)
    `NOR(scalar) == !scalar && `NAND(scalar) == !scalar &&
    `XNOR(scalar) == !scalar && `XNOR_ALT(scalar) == !scalar);
  p_wide: assert property (@(posedge clk)
    wide_nor == (phase == 0 ? 8'd1 : 8'd0) &&
    wide_nand == (phase != 3 ? 8'd1 : 8'd0) &&
    wide_xnor == (phase == 0 || phase == 2 ? 8'd1 : 8'd0));
  p_masked: assert property (@(posedge clk)
    !masked_nor && masked_nand && !masked_literal_nor && masked_literal_nand);
  p_wrong: assert property (@(posedge clk) phase == 2 |-> !xnor_lanes);
  p_unknown_nor_zero: assert property (@(posedge clk) !unknown_nor);
  p_unknown_nor_one: assert property (@(posedge clk) unknown_nor);
  p_unknown_nand_zero: assert property (@(posedge clk) !unknown_nand);
  p_unknown_nand_one: assert property (@(posedge clk) unknown_nand);
  p_unknown_xnor_zero: assert property (@(posedge clk) !unknown_xnor);
  p_unknown_xnor_one: assert property (@(posedge clk) unknown_xnor);
  p_unknown_scalar_zero: assert property (@(posedge clk) !unknown_scalar);
  p_unknown_scalar_one: assert property (@(posedge clk) unknown_scalar);
  p_unknown_driven: assert property (@(posedge clk) !unknown_driven);
`endif
endmodule

`undef NOR
`undef NAND
`undef XNOR
`undef XNOR_ALT
