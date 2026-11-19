module child(input wire [3:0] a, b, input wire enable = 1'b1,
             output wire [3:0] y, output wire open);
  assign y = enable ? a ^ b : 0;
  assign open = 1;
endmodule

module main(input wire [3:0] a, b);
  wire [3:0] y;
  child outer(.a(4'hc), .*, .open());
  p0: assert property (y == (4'hc ^ b));

  if (1) begin : g
    wire [3:0] b = 4'h3, y;
    child inner(.*, .open());
    p1: assert property (y == (a ^ 4'h3));
  end
endmodule
