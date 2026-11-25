// Spec 28 (MAP_V, plena_isa_chip_fp.sv:612-625) with a pinned stimulus, from gen_mapv.py: REP=9 shape=conj scenario=ok
module mapv(input logic clk, input logic [15:0] fp_s_in, input logic [7:0][15:0] vec_free);
  logic [6:0] cnt = 0;
  always_ff @(posedge clk) if (cnt != 127) cnt <= cnt + 1;
  logic [3:0] s_fp_op;
  always_comb case (cnt)
    5: s_fp_op = 4'h0;
    6: s_fp_op = 4'h0;
    7: s_fp_op = 4'h0;
    8: s_fp_op = 4'h9;
    9: s_fp_op = 4'h9;
    10: s_fp_op = 4'ha;
    11: s_fp_op = 4'h0;
    12: s_fp_op = 4'h0;
    13: s_fp_op = 4'hd;
    14: s_fp_op = 4'h0;
    15: s_fp_op = 4'h0;
    16: s_fp_op = 4'h0;
    17: s_fp_op = 4'h0;
    18: s_fp_op = 4'h0;
    19: s_fp_op = 4'h0;
    20: s_fp_op = 4'h0;
    21: s_fp_op = 4'h0;
    22: s_fp_op = 4'h0;
    default: s_fp_op = 4'hf;
  endcase
  wire rst = cnt == 0;
  wire v_s_out_valid = 0, stall_req_from_fp = 0, fp_sram_stall_req = 0;
  wire [3:0] fps2 = 4'h5, fpd = 4'h5; wire [31:0] addr_1 = 32'h1c0;
  wire s_map_v_valid = cnt == 23;
  logic [13:0][15:0] hist;
  always_ff @(posedge clk) hist <= {hist[12:0], fp_s_in};
  wire [7:0][15:0] fp_s_vector_out = s_map_v_valid ? {vec_free[7:1], hist[12]} : vec_free;
  wire INV_MV = 1'b1;
  p: assert property (@(posedge clk) disable iff (rst) INV_MV and (
    (s_fp_op==4'h0 && !v_s_out_valid)
    ##1 (s_fp_op==4'h0 && !v_s_out_valid)
    ##1 (s_fp_op==4'h0 && !v_s_out_valid)
    ##1 (s_fp_op==4'h9 && !stall_req_from_fp && !fp_sram_stall_req && !v_s_out_valid)
    ##1 (s_fp_op==4'h9 && !v_s_out_valid && fps2==$past(fps2))
    ##1 (s_fp_op==4'ha && !v_s_out_valid && fpd==$past(fps2,2))
    ##1 (s_fp_op==4'h0 && !v_s_out_valid)[*2]
    ##1 (s_fp_op==4'hd && !v_s_out_valid && addr_1[8:0]==$past(addr_1[8:0],3))
    ##1 (s_fp_op==4'h0 && !v_s_out_valid)[*9]
    |=> (s_map_v_valid && fp_s_vector_out[0]==$past(fp_s_in,13))));
endmodule
