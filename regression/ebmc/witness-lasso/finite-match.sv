module main(input clk);
  reg [31:0] count = 0;
  always @(posedge clk) count <= count + 1;
  completed: cover property (count == 0 ##2 count == 2);
  pending: cover property (count == 0 ##3 count == 3);
  missed: assert property (count == 0 |-> weak(##[1:2] count == 7));
endmodule
