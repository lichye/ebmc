module main(
  input bit clk,
  input bit reset,
  input bit [1:0] index,
  input bit [2:0] wide_index
);
  logic [3:0] ones = '0, one_low = '0, alternating = '0, with_x = '0;

  always @(posedge clk)
    if (reset) begin
      ones <= '1;
      one_low <= 4'b1110;
      alternating <= 4'b0101;
      with_x <= 4'b0x11;
    end

  c_reset: cover property (@(posedge clk) reset);
  p_and_ok: assert property (@(posedge clk) reset |=> &ones);
  p_and_bad: assert property (@(posedge clk) reset |=> &one_low);
  p_bit_ok: assert property (@(posedge clk) reset |=> ones[0]);
  p_bit_bad: assert property (@(posedge clk) reset |=> one_low[0]);
  p_index_ok: assert property (@(posedge clk) reset |=> alternating[index] == !index[0]);
  p_index_bad: assert property (@(posedge clk) reset |=> alternating[index] == index[0]);
  p_x_dominance: assert property (@(posedge clk) reset |=> !(&with_x));
  p_x_known_bit: assert property (@(posedge clk) reset |=> with_x[0]);
  p_x_is_zero: assert property (@(posedge clk) reset |=> !with_x[2]);
  p_x_is_one: assert property (@(posedge clk) reset |=> with_x[2]);

`ifdef CHECK_WIDE_INDEX
  p_wide_index: assert property (@(posedge clk) reset |=> ones[wide_index]);
`endif
endmodule
