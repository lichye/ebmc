module clock_reset(input logic clk, rst, data);
  logic q;
  always_ff @(posedge clk or posedge rst)
    if (rst) q <= 1'b0;
    else q <= data;

  cleared: assert property (@(posedge clk) rst |=> !q);
  sampled: assert property (@(posedge clk) !rst |=> q == $past(data));
  always_clear: assert property (@(posedge clk) !rst && data |=> !q);
endmodule
