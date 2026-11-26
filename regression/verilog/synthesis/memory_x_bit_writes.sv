// A Yosys-style byte-masked memory write: one bit-slice write per bit at a
// runtime address, the bits outside the enabled byte written with x under a
// disabled enable (write_verilog of a memwr cell with a byte mask).
module main(input clk, input logic [1:0] addr, input logic [7:0] wdata,
            input logic [1:0] wmask);
  reg [15:0] mem [3:0];
  wire [15:0] en0 = wmask[0] ? 16'h00ff : 16'h0000;
  wire [15:0] en1 = wmask[1] ? 16'hff00 : 16'h0000;
  always @(posedge clk) begin
    for (int b = 0; b < 16; b++) begin
      if (en0[b]) mem[addr][b] <= b < 8 ? wdata[b % 8] : 1'bx;
      if (en1[b]) mem[addr][b] <= b >= 8 ? wdata[b % 8] : 1'bx;
    end
  end
  p_low: assert property (@(posedge clk) wmask == 2'b01 |=> mem[$past(addr)][7:0] == $past(wdata));
  p_high: assert property (@(posedge clk) wmask == 2'b10 |=> mem[$past(addr)][15:8] == $past(wdata));
  p_keep: assert property (@(posedge clk) wmask == 2'b01 |=> mem[$past(addr)][15:8] == $past(mem[addr][15:8]));
endmodule
