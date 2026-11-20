module main(input clk);
  reg [3:0] tick = 0;
  always @(posedge clk) tick <= tick + 1;

  wire start = tick == 1, reply = tick == 4;
  wire mid = tick == 3, late = tick == 5, missed = tick == 8;

  p_reply: assert property (@(posedge clk) disable iff (1'b0)
    start |-> ##[1:3] reply);
  p_missing: assert property (@(posedge clk) disable iff (1'b0)
    start |-> ##[1:3] missed);
  p_mid: assert property (@(posedge clk) disable iff (mid)
    start |-> ##[1:3] missed);
  p_late: assert property (@(posedge clk) disable iff (late)
    start |-> ##[1:3] missed);
  p_trigger: assert property (@(posedge clk) disable iff (start)
    start |=> 1'b0);
endmodule
