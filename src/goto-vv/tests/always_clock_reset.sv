module always_clock_reset(input wire clk, rst, rst_n, data);
  reg high_q, low_q;
  always @(posedge clk or posedge rst) begin
    if (rst)
      high_q <= 1'b0;
    else
      high_q <= data;
  end
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      low_q <= 1'b1;
    else
      low_q <= !data;
  end

  p_high_reset: assert property (@(posedge clk) rst |=> !high_q);
  p_high_update: assert property (@(posedge clk) !rst |=> high_q == $past(data));
  p_low_reset: assert property (@(posedge clk) !rst_n |=> low_q);
  p_low_update: assert property (@(posedge clk) rst_n |=> low_q == !$past(data));
  p_high_bad: assert property (@(posedge clk) rst |=> high_q);
  p_low_bad: assert property (@(posedge clk) !rst_n |=> !low_q);
endmodule
