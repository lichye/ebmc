  // In the bound-20 experiment, state 0 is reset, state 1 issues EXP, and
  // every other state issues STALL. The counter cannot wrap in the window.
  bit [4:0] m2_step = 0;
  always_ff @(posedge clk) m2_step <= m2_step + 1'b1;
  m2_drive_op: assume property (@(posedge clk)
      operation == ((m2_step == 1) ? EXP_FP : STALL_S_FP));
  m2_drive_data: assume property (@(posedge clk) data_in == 12'h3f0);
  m2_drive_addr: assume property (@(posedge clk) reg_waddr == 3'd5);

  m2_live: cover property (@(posedge clk) !rst && data_out_valid);
  m2_latency: assert property (@(posedge clk) !rst |->
      (data_out_valid == (m2_step == 5'd19)));
  m2_value: assert property (@(posedge clk) !rst && data_out_valid |-> data_out == 12'h423);
  m2_opposite: assert property (@(posedge clk) !rst && data_out_valid |-> data_out != 12'h423);
