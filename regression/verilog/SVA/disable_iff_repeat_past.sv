module top(input bit clk);
  bit [3:0] tick = 0;
  always @(posedge clk)
    if (tick < 9) tick <= tick + 1;

  wire idle = tick >= 1 && tick <= 3;
  wire store = tick == 4;
  wire readback = tick == 6;
  wire address = store || readback;
  wire value = tick == 7;

  sequence operations;
    idle[*3] ##1 store ##1 (tick == 5)
      ##1 (readback && address == $past(address, 2));
  endsequence

  good: assert property (@(posedge clk) disable iff (tick == 0)
    operations |=> value == $past(store, 3));
  wrong_data: assert property (@(posedge clk) disable iff (tick == 0)
    operations |=> value != $past(store, 3));
  aborted: assert property (@(posedge clk) disable iff (tick == 5)
    operations |=> value != $past(store, 3));
  antecedent: cover property (@(posedge clk) operations);
endmodule
