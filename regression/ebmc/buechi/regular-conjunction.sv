module main(input bit clk);
  bit [2:0] cycle = 0;
  always @(posedge clk)
    if (cycle != 7)
      cycle <= cycle + 1;

  wire rst = cycle == 0;
  wire rst_mid = rst || cycle == 2;
  wire start = cycle == 1;
  wire finish = cycle == 3;
  wire hold = cycle == 2;
  wire inv = !rst;

  // The predicate conjunct fails immediately even if the consequent holds.
  immediate: assert property (@(posedge clk) disable iff (rst)
    !start and (start ##1 hold[*0:$] ##1 finish |=> 1'b1));

  // One window sample. Evaluate the consequent after the match endpoint.
  deferred: assert property (@(posedge clk) disable iff (rst)
    inv and (start ##1 hold[*0:$] ##1 finish |=> 1'b0));

  // Reset during the window cancels the pending attempt.
  cancelled: assert property (@(posedge clk) disable iff (rst_mid)
    inv and (start ##1 hold[*0:$] ##1 finish |=> 1'b0));

  // No window samples. The suffix can start on the very next sample.
  empty_window: assert property (@(posedge clk) disable iff (rst)
    inv and (start ##1 hold[*0:$] ##1 hold |-> 1'b0));
endmodule
