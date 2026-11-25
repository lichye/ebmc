// explicit two-state conversions of non-constant values that contain x
module main(input clk, input logic [1:0] t);
  typedef bit [1:0] b2_t;
  logic [1:0] a = 0, b = 0, c = 0;
  always_ff @(posedge clk) a <= b2_t'({1'bx, t[0]});
  always_ff @(posedge clk) b <= 2'(bit'(1'bx));
  always_ff @(posedge clk) c <= {bit'(1'bx), t[1]};
  c_a: assert property (@(posedge clk) a[1] == 0);
  c_b: assert property (@(posedge clk) b == 0);
  c_c: assert property (@(posedge clk) c[1] == 0);
endmodule
