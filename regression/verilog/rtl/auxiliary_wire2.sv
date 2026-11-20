// A masked write with a non-constant address, as in a RAM with a bit
// mask (prim_generic_ram_1p in PLENA). Every write refers to the
// previous value of the memory as the array that is updated and as the
// element that is modified, and the conditional write refers to it once
// more. The value of the memory is bound to auxiliary wires.
module main(input clk, input [1:0] addr, input [11:0] wdata, input [11:0] wmask);

  reg [11:0] mem[4];
  reg [11:0] ref_mem[4];

  initial for (int j = 0; j < 4; j++) begin
    mem[j] = 0;
    ref_mem[j] = 0;
  end

  always @(posedge clk)
    for (int i = 0; i < 12; i++)
      if (wmask[i])
        mem[addr][i] <= wdata[i];

  always @(posedge clk)
    ref_mem[addr] <= (ref_mem[addr] & ~wmask) | (wdata & wmask);

  p0: assert property (mem[0] == ref_mem[0] && mem[1] == ref_mem[1] &&
                       mem[2] == ref_mem[2] && mem[3] == ref_mem[3]);

endmodule
