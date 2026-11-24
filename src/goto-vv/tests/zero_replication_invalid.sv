module zero_replication_empty(input logic clk);
  wire value = {{0{1'b0}}, {0{1'b1}}};
  p: assert property (@(posedge clk) value);
endmodule

module zero_replication_standalone(input logic clk);
  wire value = {0{1'b0}};
  p: assert property (@(posedge clk) value);
endmodule
