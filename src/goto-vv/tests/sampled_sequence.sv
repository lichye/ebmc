module sampled_sequence(input bit clk);
  bit [3:0] step = 0;
  logic [3:0] payload = 4'b1000;
  bit [3:0] zero = 0;
  always_ff @(posedge clk) begin
    step <= step + 1'b1;
    if (step == 1) payload <= 4'b0000;
  end

  // On the first sample EBMC compares the packed values with zero.
  first_zero: assert property (@(posedge clk) step == 0 |-> $stable(zero));
  first_nonzero: assert property (@(posedge clk) step == 0 |-> $stable(payload));

  // The high bit changes at the second edge. The low bit never changes.
  hold: assert property (@(posedge clk) step == 1 |-> $stable(payload));
  change: assert property (@(posedge clk) step == 2 |-> $stable(payload));
  after_reset: assert property (@(posedge clk) disable iff (step == 0)
                                step == 1 |-> $stable(payload));
  after_reset_bad: assert property (@(posedge clk) disable iff (step == 1)
                                    step == 2 |-> $stable(payload));

  // An inclusive three-sample window, including both endpoints and the interior.
  window_ok: assert property (@(posedge clk) step == 0 |->
                             (step < 3) throughout (1'b1 ##2 1'b1));
  window_first: assert property (@(posedge clk) step == 0 |->
                                (step != 0) throughout (1'b1 ##2 1'b1));
  window_middle: assert property (@(posedge clk) step == 0 |->
                                 (step != 1) throughout (1'b1 ##2 1'b1));
  window_last: assert property (@(posedge clk) step == 0 |->
                               (step != 2) throughout (1'b1 ##2 1'b1));
  window_cancel: assert property (@(posedge clk) disable iff (step == 1)
                                 step == 0 |-> (step != 2) throughout (1'b1 ##2 1'b1));
  antecedent_ok: assert property (@(posedge clk)
                                 (step < 2) throughout (step == 0 ##1 1'b1) |=> step == 2);
  antecedent_bad: assert property (@(posedge clk)
                                  (step < 2) throughout (step == 0 ##1 1'b1) |=> step == 1);
  window_live: cover property (@(posedge clk) (step < 2) throughout (step == 0 ##1 step == 1));
  single_live: cover property (@(posedge clk) (step == 2) throughout 1'b1);
endmodule
