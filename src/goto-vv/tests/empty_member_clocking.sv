module empty_member_clocking(input logic clk, input logic value);
  clocking cb @(posedge clk);
    input value;
  endclocking
  p: assert property (@(posedge clk) value);
endmodule
