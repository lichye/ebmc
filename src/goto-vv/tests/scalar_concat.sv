module scalar_concat(input logic clk, input logic a);
  wire x = {a};
  wire z = {1{1'b0}};
  wire o = {{1{1'b1}}};
  same: assert property (@(posedge clk) x == a);
  zero: assert property (@(posedge clk) !z);
  one: assert property (@(posedge clk) o);
  always_high: assert property (@(posedge clk) x);
endmodule
