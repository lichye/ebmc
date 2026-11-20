// The masked write of auxiliary_wire2 with the default size of
// prim_generic_ram_1p in PLENA: 32 mask bits, 128 words. Without the
// auxiliary wires, the tree of the expression for the memory has about
// 3^32 nodes.
module main(input clk, input req_i, input write_i, input [6:0] addr_i,
            input [31:0] wdata_i, input [31:0] wmask_i,
            output reg [31:0] rdata_o);

  reg [31:0] mem[128];

  initial for (int j = 0; j < 128; j++) mem[j] = 0;

  always @(posedge clk) begin
    if (req_i) begin
      if (write_i) begin
        for (int i = 0; i < 32; i++)
          if (wmask_i[i])
            mem[addr_i][i] <= wdata_i[i];
      end else
        rdata_o <= mem[addr_i];
    end
  end

  p0: assert property (mem[5] == 0);

endmodule
