module strict_indexed_leaf(input logic [2:0] d, output wire [2:0] q);
  assign q = ~d;
endmodule

module strict_indexed_write(input logic clk, input logic [2:0] x, y);
  localparam LO = 4, HI = 11, WIDTH = 3;
  wire [HI:LO] down;
  wire [LO:HI] up;
`ifdef EBMC_FIXED_REFERENCE
  strict_indexed_leaf down_lo(x, down[6:4]);
  strict_indexed_leaf down_hi(y, down[11:9]);
  strict_indexed_leaf up_lo(x, up[4:6]);
  strict_indexed_leaf up_hi(y, up[9:11]);
  assign down[8:7] = 2'b01;
  assign up[7:8] = 2'b10;
`else
  strict_indexed_leaf down_lo(x, down[LO +: WIDTH]);
  strict_indexed_leaf down_hi(y, down[HI -: WIDTH]);
  strict_indexed_leaf up_lo(x, up[LO +: WIDTH]);
  strict_indexed_leaf up_hi(y, up[HI -: WIDTH]);
  assign down[LO + WIDTH +: 2] = 2'b01;
  assign up[HI - WIDTH -: 2] = 2'b10;
`endif
  down_good: assert property (@(posedge clk) down == {~y, 2'b01, ~x});
  up_good: assert property (@(posedge clk) up == {~x, 2'b10, ~y});
  down_wrong: assert property (@(posedge clk) down == {~y, 2'b11, ~x});
  up_wrong: assert property (@(posedge clk) up == {~x, 2'b00, ~y});
endmodule
