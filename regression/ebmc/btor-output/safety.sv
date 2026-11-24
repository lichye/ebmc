module main(input bit clk, input bit [2:0] shift);
  bit signed [3:0] data = -4;
  always @(posedge clk) data <= data >>> shift;
  a_shift: assume property (shift < 4);
  p_negative: assert property (data < 0);
  p_small: assert property (data == -4);
endmodule
