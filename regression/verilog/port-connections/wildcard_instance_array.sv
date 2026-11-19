module leaf(input wire a, output wire y);
  assign y=~a;
endmodule
module main(input wire [1:0] a);
  wire [1:0] y;
  leaf g[1:0](.*);
  p0: assert property (y==~a);
endmodule
