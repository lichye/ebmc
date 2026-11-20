module bit_driver(input wire clk, input wire d, output reg q);
  initial q = 0;
  always @(posedge clk)
    q <= d;
endmodule

module main(input wire clk, input wire [2:0] d);
  wire [2:0] valid;
  reg [2:0] expected = 0;
  always @(posedge clk)
    expected <= d;

  bit_driver u0(.clk(clk), .d(d[0]), .q(valid[0]));
  for(genvar i = 0; i < 2; ++i) begin
    bit_driver u(.clk(clk), .d(d[i + 1]), .q(valid[i + 1]));
  end

  p_hold: assert property (valid == expected);
  p_fail: assert property (valid != expected);
endmodule
