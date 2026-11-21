module eventual(input clk, alarm);
  bit launch = 1;
  always @(posedge clk) launch <= 0;
`ifndef ALLOW_SILENCE
  debt: assume property (@(posedge clk) launch |-> ##8 alarm);
`endif
  wanted: assert property (@(posedge clk) s_eventually alarm);
endmodule
