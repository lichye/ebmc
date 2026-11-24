module empty_member_leaf(input logic clk, input logic value);
  p_value: assert property (@(posedge clk) value);
  p_wrong: assert property (@(posedge clk) !value);
endmodule

module empty_member(input logic clk);
  generate;
    if (1) begin : selected
      empty_member_leaf u_leaf(.clk(clk), .value(1'b1));
    end
  endgenerate
endmodule
