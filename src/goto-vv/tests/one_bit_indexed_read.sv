module one_bit_indexed_read(input logic clk, input bit [3:0] wmask_i,
    input bit [1:0] base);
  localparam int DataBitsPerMask = 1;
  wire [3:0] mask;
  for (genvar k = 0; k < 4; k++) begin : gen_wmask
    assign mask[k] = &wmask_i[k*DataBitsPerMask +: DataBitsPerMask];
  end
  wire read_up = wmask_i[base +: 1];
  wire read_down = wmask_i[base -: 1];
  mask_ok: assert property (@(posedge clk) mask == wmask_i);
  slice_ok: assert property (@(posedge clk)
    read_up == wmask_i[base] && read_down == wmask_i[base]);
endmodule
