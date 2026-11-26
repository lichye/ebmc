module packed_default_effect(input logic clk, output logic [3:0] q);
  integer count;
  always @(posedge clk)
    q <= '{default: count++};
endmodule
