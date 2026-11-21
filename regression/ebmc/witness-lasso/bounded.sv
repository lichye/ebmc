module bounded(input clk);
  bit [2:0] tick = 0;
  always @(posedge clk) if (tick < 5) tick <= tick + 1;
  good: assert property (@(posedge clk) tick < 3);
  reached: cover property (@(posedge clk) tick == 3);
endmodule
