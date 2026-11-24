module main(input bit clk, input bit go, input bit wrong, input bit ready);
  bit [1:0] step = 2'd0;
  always @(posedge clk)
    if (go && step < 2'd2)
      step <= step + 2'd1;
  without_wrong: assume property (!wrong);
  with_ready: assume property (ready);
  z_late: assert property (step != 2'd2);
  a_safe: assert property (step <= 2'd2 && !wrong && ready);
  m_immediate: assert property (!go);
endmodule
