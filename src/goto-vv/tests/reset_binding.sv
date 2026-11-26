module reset_binding_child(input wire data, output wire result);
  assign result = data;
endmodule

module top(input wire clk, rst_ni, data,
           input wire [1:0] wide, output wire carried);
  bit left_initial = 0;
  always @(posedge clk) left_initial <= 1;

  reg q;
  always_ff @(posedge clk or negedge rst_ni)
    if (!rst_ni) q <= 0;
    else q <= 1;

  reset_binding_child u(.data(data), .result(carried));

  p_low0: assert property (@(posedge clk) !left_initial |-> !rst_ni);
  p_low1: assert property (@(posedge clk) left_initial |-> rst_ni);
  p_high0: assert property (@(posedge clk) !left_initial |-> rst_ni);
  p_high1: assert property (@(posedge clk) left_initial |-> !rst_ni);

  p_reset_effect: assert property (@(posedge clk) !left_initial |=> !q);
  c_low: cover property (@(posedge clk)
    (!left_initial && !rst_ni) ##1 (left_initial && rst_ni && !q));
  c_high: cover property (@(posedge clk)
    (!left_initial && rst_ni) ##1 (left_initial && !rst_ni));
endmodule
