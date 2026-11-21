  // The environment resets frame 0. The counter also anchors the schedule before reset.
  bit [6:0] test_frame = 0;
`ifdef REFILL32
  localparam logic [6:0] final_frame = 7'd75;
`else
  localparam logic [6:0] final_frame = 7'd11;
`endif
  always_ff @(posedge clk)
    if (test_frame != final_frame) test_frame <= test_frame + 7'd1;
`ifdef ALT_DATA
  localparam logic [31:0] first_word = 32'h5a3ce0f8;
`else
  localparam logic [31:0] first_word = 32'ha5c31f07;
`endif
  localparam logic [31:0] last_word  = 32'h59d8e20b;
  localparam logic [19:0] base_pc = 20'h00100;
`ifdef REFILL32
  wire is_request = test_frame >= 7'd3 && test_frame <= 7'd65 && test_frame[0];
  wire is_reply = test_frame >= 7'd4 && test_frame <= 7'd66 && !test_frame[0];
`else
  wire is_request = test_frame == 7'd3;
  wire is_reply = test_frame == 7'd4;
`endif
  wire [4:0] response_index = (test_frame - 7'd4) >> 1;
  wire [31:0] response_data = response_index == 5'd0 ? first_word :
                              response_index == 5'd31 ? last_word :
                              32'hc0190000 | {27'b0, response_index};
  logic [4:0] response_source = 5'd0;
  always_ff @(posedge clk)
    if (instr_tl_a_valid_o && instr_tl_a_ready_i)
      response_source <= instr_tl_a_o[169:165];
`ifdef OPCODE_CONTROL
  localparam logic [2:0] response_opcode = 3'd0;
`else
  localparam logic [2:0] response_opcode = 3'd1;
`endif
  wire [49:0] scheduled_d = is_reply ?
    {response_opcode,3'b000,4'd2,response_source,1'b0,1'b0,1'b0,response_data} :
    {3'd0,15'b0,32'hf06d481a};
  drive_a: assume property (@(posedge clk) instr_tl_a_ready_i == is_request);
  drive_d_valid: assume property (@(posedge clk) instr_tl_d_valid_i == is_reply);
  drive_d_packet: assume property (@(posedge clk) instr_tl_d_i == scheduled_d);
  drive_pc: assume property (@(posedge clk) pc ==
    (test_frame >= 7'd69 ? base_pc + 20'd124 : base_pc));
  quiet_b: assume property (@(posedge clk) !instr_tl_b_valid_i);
  wire a_accept = instr_tl_a_valid_o && instr_tl_a_ready_i;
  wire d_accept = instr_tl_d_valid_i && instr_tl_d_ready_o;
  legal_pair: cover property (@(posedge clk)
    (test_frame == 7'd3 && a_accept && instr_tl_a_o[179:177] == 3'd4) ##1
    (d_accept && instr_tl_d_i[49:47] == 3'd1 && instr_tl_d_i[31:0] == first_word));
  first_get: assert property (@(posedge clk) test_frame == 7'd3 |->
    a_accept);
  a_opcode_diagnostic: assert property (@(posedge clk) test_frame == 7'd3 |->
    instr_tl_a_o[179:177] == 3'd4);
  first_d: cover property (@(posedge clk) test_frame == 7'd4 &&
    d_accept && instr_tl_d_i[49:47] == 3'd1 && instr_tl_d_i[31:0] == first_word);
  capture: assert property (@(posedge clk) test_frame == 7'd5 |->
    !rst && u_dut.fetch_data_valid && u_dut.fetch_data == first_word &&
    u_dut.fetch_data == $past(instr_tl_d_i[31:0]) &&
    $past(instr_tl_d_i[49:47]) == 3'd1 && instr_tl_d_i[49:47] == 3'd0);
  capture_inverse: assert property (@(posedge clk) test_frame == 7'd5 |->
    !u_dut.fetch_data_valid || u_dut.fetch_data != first_word);
  opcode_zero_handshake: cover property (@(posedge clk) test_frame == 7'd4 &&
    d_accept && instr_tl_d_i[49:47] == 3'd0 && instr_tl_d_i[31:0] == first_word);
  opcode_zero_drops: assert property (@(posedge clk) test_frame == 7'd5 |->
    !u_dut.fetch_data_valid && u_dut.fetch_data == 32'd0);
  opcode_zero_inverse: assert property (@(posedge clk) test_frame == 7'd5 |->
    u_dut.fetch_data_valid);
  before_capture: assert property (@(posedge clk) test_frame == 7'd4 |-> !u_dut.fetch_data_valid);
`ifdef REFILL32
  bit all_requested = 1'b1;
  wire [4:0] request_index = (test_frame - 7'd3) >> 1;
  wire [127:0] expected_address = 128'd14656 + {121'b0, request_index, 2'b00};
  always_ff @(posedge clk)
    if (is_request && !(a_accept && instr_tl_a_o[179:177] == 3'd4 &&
       instr_tl_a_o[173:170] == 4'd2 && instr_tl_a_o[164:37] == expected_address))
      all_requested <= 1'b0;
  full_bus_witness: cover property (@(posedge clk) test_frame == 7'd70 &&
    all_requested && instruction_ready && instruction_out == last_word);
  accept_all_reads: assert property (@(posedge clk) is_request |-> a_accept);
  accept_all_replies: assert property (@(posedge clk) is_reply |-> d_accept);
  first_slot: assert property (@(posedge clk) test_frame == 7'd6 |->
    u_dut.instr_buffer[0] == first_word && !u_dut.buffer_valid);
  last_capture: assert property (@(posedge clk) test_frame == 7'd67 |->
    u_dut.fetch_data_valid && u_dut.fetch_data == last_word &&
    u_dut.fill_counter == 5'd31 && !u_dut.buffer_valid);
  last_slot: assert property (@(posedge clk) test_frame == 7'd68 |->
    u_dut.buffer_valid && u_dut.instr_buffer[31] == last_word && !instruction_ready);
  read_first: assert property (@(posedge clk) test_frame == 7'd69 |->
    instruction_ready && instruction_addr == base_pc && instruction_out == first_word);
  read_last: assert property (@(posedge clk) test_frame == 7'd70 |->
    instruction_ready && instruction_addr == base_pc + 20'd124 && instruction_out == last_word);
  read_first_inverse: assert property (@(posedge clk) test_frame == 7'd69 |->
    !instruction_ready || instruction_out != first_word);
  read_last_inverse: assert property (@(posedge clk) test_frame == 7'd70 |->
    !instruction_ready || instruction_out != last_word);
  full_reached: cover property (@(posedge clk) test_frame == 7'd70 && instruction_ready &&
    instruction_addr == base_pc + 20'd124 && instruction_out == last_word);
`endif
