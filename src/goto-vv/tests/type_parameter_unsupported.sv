module type_parameter_unsupported_leaf #(parameter type T = logic) ();
  T data;
endmodule

module type_parameter_unsupported(input logic clk);
  type_parameter_unsupported_leaf #(.T(string)) bad();
  p: assert property (@(posedge clk) 1'b1);
endmodule
