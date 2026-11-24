module main(input clk, input seed);
  reg a, b, seen;
  initial begin
    a = seed;
    b = !seed;
    seen = 0;
  end
  always @(posedge clk) begin
    a <= 0;
    b <= 0;
    seen <= 1;
  end
  first_only: assert property (seen || a != b);
  every_frame: assert property (a != b);
endmodule
