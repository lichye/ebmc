module main(input bit d);
`ifdef NONCONSTANT_PARAMETER
  localparam P = $isunknown(d + 'x);
  p: assert final (P == 0);
`else
  localparam logic [1:0] MASK = 2'b10;
  localparam HAS_X = $isunknown(MASK);
  localparam WIDTH_HAS_X = $isunknown($bits(d));
  wire detected = $isunknown(d ? 1'bx : 1'b0);
  p0: assert final (detected == d);
  p1: assert final (!HAS_X && !WIDTH_HAS_X);
  p2: assert final (!detected);
`endif
endmodule
