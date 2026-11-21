  m2_reset_one: cover property (@(posedge clk) rst);
  m2_reset_two: cover property (@(posedge clk) rst throughout (1'b1 ##1 1'b1));
  m2_isolated_reset: cover property (@(posedge clk) !rst ##1 rst ##1 !rst);
