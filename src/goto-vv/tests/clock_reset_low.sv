module clock_reset_low(input wire clk, rst_n, data);
  reg q;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n)
      q <= 1'b1;
    else
      q <= !data;

  p_reset: assert property (@(posedge clk) !rst_n |=> q);
  p_hold: assert property (@(posedge clk) !rst_n ##1 !rst_n |=> q);
  p_release: assert property (@(posedge clk) !rst_n ##1 rst_n |-> q);
  p_update: assert property (@(posedge clk) rst_n |=> q == !$past(data));
  p_wrong: assert property (@(posedge clk) !rst_n |=> !q);
endmodule
