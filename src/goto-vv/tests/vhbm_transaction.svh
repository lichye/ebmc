// Frame 0 is reset. Stop after both slices have drained to allow a short loop.
  bit [3:0] frame = 0;
  always_ff @(posedge clk)
    if (frame != 4'd13) frame <= frame + 4'd1;
`ifdef LOW_PRECISION
  localparam bit select_low = 1'b1;
  localparam logic [3:0] scale_reply = 4'd5, element_reply = 4'd8, join_frame = 4'd11;
  localparam logic [63:0] element_mark = 64'h57c6a19b83024ed5;
  localparam logic [7:0] scale_mark = 8'hb6;
`else
  localparam bit select_low = 1'b0;
  localparam logic [3:0] scale_reply = 4'd8, element_reply = 4'd5, join_frame = 4'd10;
  localparam logic [63:0] element_mark = 64'hd16427ba91050ce3;
  localparam logic [7:0] scale_mark = 8'ha7;
`endif
  wire active_loaded_valid = select_low ? u_dut.loaded_low_precision_element_valid : u_dut.loaded_high_precision_element_valid;
  wire inactive_loaded_valid = select_low ? u_dut.loaded_high_precision_element_valid : u_dut.loaded_low_precision_element_valid;
  wire [63:0] active_loaded_data = select_low ? u_dut.loaded_low_precision_element : u_dut.loaded_high_precision_element;
  wire [63:0] active_out = select_low ? prefetch_low_precision_element : prefetch_high_precision_element;
  wire [7:0] sampled_low_mask = u_dut.low_precision_tl_element_a.mask;
  wire [63:0] idle_element = 64'hf06d48c239bb51ac;
  wire [7:0] idle_scale = 8'h39;
  wire scale_d_accept = host_scale_d_valid_i && host_scale_d_ready_o;
  wire element_d_accept = host_element_d_valid_i && host_element_d_ready_o;
  logic [4:0] scale_source = 0, element_source = 0;
  always_ff @(posedge clk) begin
    if (frame == 3) scale_source <= host_scale_a_o[142:138];
    if (frame == 4) element_source <= host_element_a_o[205:201];
  end
  wire [25:0] scale_packet = {3'd1,3'd0,4'd0,scale_source,3'b000,scale_mark};
  wire [81:0] element_packet = {3'd1,3'd0,4'd3,element_source,3'b000,element_mark};
  schedule_cmd: assume property (@(posedge clk) hbm_prefetch_en == (frame == 1) &&
    precision_select == select_low && !hbm_write_en && !stride_mode &&
    hbm_raddr == 128'h1000 && addr_offset == 0 && scale_offset == 32'h80 && stride_offset == 0);
  schedule_a: assume property (@(posedge clk) host_scale_a_ready_i == (frame == 3) &&
    host_element_a_ready_i == (frame == 4));
  schedule_e: assume property (@(posedge clk) host_element_d_valid_i == (frame == element_reply) &&
    host_element_d_i == (frame == element_reply ? element_packet : {18'b0,idle_element}));
  schedule_s: assume property (@(posedge clk) host_scale_d_valid_i == (frame == scale_reply) &&
    host_scale_d_i == (frame == scale_reply ? scale_packet : {18'b0,idle_scale}));
  quiet_b: assume property (@(posedge clk) !host_scale_b_valid_i && !host_element_b_valid_i);
  bit reads_seen = 1;
  always_ff @(posedge clk) begin
    if (frame == 3 && !(host_scale_a_valid_o && host_scale_a_ready_i &&
      host_scale_a_o[152:150] == 4 && host_scale_a_o[146:143] == 0)) reads_seen <= 0;
    if (frame == 4 && !(host_element_a_valid_o && host_element_a_ready_i &&
      host_element_a_o[215:213] == 4 && host_element_a_o[209:206] == 3)) reads_seen <= 0;
  end
  selected: assert property (@(posedge clk) frame >= 2 |-> u_dut.recorded_precision_select == select_low);
  low_mask: assert property (@(posedge clk) sampled_low_mask == 8'hff);
  scale_accept: assert property (@(posedge clk) frame == scale_reply |-> scale_d_accept &&
    host_scale_d_i[25:23] == 1 && host_scale_d_i[7:0] == scale_mark);
  element_accept: assert property (@(posedge clk) frame == element_reply |-> element_d_accept &&
    host_element_d_i[81:79] == 1 && host_element_d_i[63:0] == element_mark);
  scale_capture: assert property (@(posedge clk) frame == scale_reply + 1 |-> u_dut.loaded_scale_valid &&
    u_dut.loaded_scale == $past(host_scale_d_i[7:0]) && u_dut.loaded_scale == scale_mark &&
    host_scale_d_i[7:0] == idle_scale);
  scale_slice: assert property (@(posedge clk) frame == scale_reply + 2 |-> u_dut.prefetch_scale_data_valid &&
    prefetch_scale == $past(u_dut.loaded_scale) && prefetch_scale == scale_mark);
  element_capture: assert property (@(posedge clk) frame == element_reply + 2 |-> active_loaded_valid &&
    active_loaded_data == element_mark && !inactive_loaded_valid);
  element_slice: assert property (@(posedge clk) frame == element_reply + 3 |-> u_dut.prefetch_element_data_valid &&
    active_out == element_mark);
  no_early_join: assert property (@(posedge clk) frame >= 1 && frame < join_frame |-> !prefetch_data_valid);
  hold_first: assert property (@(posedge clk) frame >= (select_low ? scale_reply + 2 : element_reply + 3) &&
    frame < join_frame |-> select_low ?
      (u_dut.prefetch_scale_data_valid && !u_dut.prefetch_scale_data_ready && prefetch_scale == scale_mark) :
      (u_dut.prefetch_element_data_valid && !u_dut.prefetch_element_data_ready && active_out == element_mark));
  join_value: assert property (@(posedge clk) frame == join_frame |-> prefetch_data_valid &&
    prefetch_data_ready && active_out == element_mark && prefetch_scale == scale_mark);
  joined_once: assert property (@(posedge clk) frame > join_frame |-> !prefetch_data_valid);
  scale_inverse: assert property (@(posedge clk) frame == scale_reply + 1 |-> u_dut.loaded_scale != scale_mark);
  join_inverse: assert property (@(posedge clk) frame == join_frame |-> !prefetch_data_valid ||
    active_out != element_mark || prefetch_scale != scale_mark);
  scale_handshake: cover property (@(posedge clk) frame == scale_reply && scale_d_accept &&
    host_scale_d_i[25:23] == 1 && host_scale_d_i[7:0] == scale_mark);
  transaction: cover property (@(posedge clk) frame == join_frame && reads_seen && prefetch_data_valid &&
    prefetch_data_ready && active_out == element_mark && prefetch_scale == scale_mark);
