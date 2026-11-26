module gated(input wire clk_i, rst_ni, test_enable_i, data);
  wire gclk, synced;
  wire core_reset = ~(test_enable_i ? rst_ni : synced);
  p_env1: assert property (@(posedge clk_i) !test_enable_i);

  reg q;
  always @(posedge gclk or posedge core_reset)
    if (core_reset) q <= 0; else q <= data;

  RstSync sync(.clk_i(clk_i), .rstn_i(rst_ni), .clk_en(1'b1),
               .te(test_enable_i), .clk_o(gclk), .rstn_o(synced));

  p_env2: assert property (@(posedge clk_i) rst_ni ##1 !rst_ni |=> !rst_ni);
  p_start: assert property (@(posedge clk_i) !rst_ni |-> !q);
  bit first = 1;
  always @(posedge clk_i) first <= 0;
  p_initial_two: assert property (@(posedge clk_i) first |-> (!rst_ni ##1 !rst_ni));
  p_released: cover property (@(posedge clk_i)
    !rst_ni ##1 !rst_ni ##1 rst_ni [*5] ##1 q);
  p_bad: assert property (@(posedge clk_i)
    !rst_ni ##1 !rst_ni ##1 rst_ni [*5] |=> !q);
endmodule
