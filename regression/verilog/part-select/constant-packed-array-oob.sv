module packed_oob(input logic clk, input logic [7:0] d);
  wire [2:1][3:0] down = d;
  wire [-1:0][3:0] up = d;
  wire [7:0] dp = down[3:2];
  wire [7:0] ap = up[0 +: 2];
  p_in: assert property (@(posedge clk) dp[3:0] == d[7:4] && ap[7:4] == d[3:0]);
  p_unknown: assert property (@(posedge clk)
    down[0] === 4'bxxxx &&
    down[3:2] === {4'bxxxx, d[7:4]} &&
    up[0 +: 2] === {d[3:0], 4'bxxxx});
endmodule
