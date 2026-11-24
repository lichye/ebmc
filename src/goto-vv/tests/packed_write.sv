module packed_write(
  input logic clk, load_valid,
  input logic [2:0] load_cnt,
  input logic [7:0] load_scale
);
  logic [3:0][7:0] a_scale_buf = 32'h129B46D8;
  logic [3:0][7:0] reference_buf = 32'h129B46D8;

  always_ff @(posedge clk)
    if (load_valid)
`ifdef NATIVE_STATIC_WRITE_REFERENCE
      case (load_cnt)
        0: a_scale_buf[0] <= load_scale;
        1: a_scale_buf[1] <= load_scale;
        2: a_scale_buf[2] <= load_scale;
        3: a_scale_buf[3] <= load_scale;
        default: ;
      endcase
`else
      a_scale_buf[load_cnt] <= load_scale;
`endif

  always_ff @(posedge clk)
    if (load_valid) begin
      if (load_cnt == 0) reference_buf[0] <= load_scale;
      if (load_cnt == 1) reference_buf[1] <= load_scale;
      if (load_cnt == 2) reference_buf[2] <= load_scale;
      if (load_cnt == 3) reference_buf[3] <= load_scale;
    end

  for (genvar lane = 0; lane < 4; lane++) begin : g_lane
    p_selected: assert property (@(posedge clk)
      (load_valid && load_cnt == lane) |=> a_scale_buf[lane] == $past(load_scale));
  end
  p_all: assert property (@(posedge clk) a_scale_buf == reference_buf);
  p_oob: assert property (@(posedge clk)
    (load_valid && load_cnt >= 4) |=> a_scale_buf == $past(a_scale_buf));
  p_wrong: assert property (@(posedge clk)
    (load_valid && load_cnt == 2 && load_scale == 8'hA7)
    |=> a_scale_buf[2] != $past(load_scale));
  p_wrong_oob: assert property (@(posedge clk)
    (load_valid && load_cnt == 6 && load_scale == 8'h55)
    |=> a_scale_buf != $past(a_scale_buf));
endmodule
