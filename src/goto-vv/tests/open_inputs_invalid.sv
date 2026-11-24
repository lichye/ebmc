module ref_leaf(ref logic data, output wire copied);
  assign copied = data;
endmodule

module inout_leaf(inout wire data, output wire copied);
  assign copied = data;
endmodule

module ordinary_leaf(input wire data, output wire copied);
  assign copied = data;
endmodule

module open_input_bad_ref(input logic clk);
  logic data;
  wire copied;
  ref_leaf u(.data(data), .copied(copied));
  p: assert property (@(posedge clk) copied == data);
endmodule

module open_input_bad_inout(input logic clk);
  wire copied;
  inout_leaf u(.data(), .copied(copied));
  p: assert property (@(posedge clk) copied);
endmodule

`ifdef BAD_IMPLICIT
module open_input_bad_implicit(input logic clk);
  wire copied;
  ordinary_leaf u(.data, .copied(copied));
  p: assert property (@(posedge clk) copied);
endmodule
`endif

`ifdef BAD_NAME
module open_input_bad_name(input logic clk);
  wire copied;
  ordinary_leaf u(.typo(), .copied(copied));
  p: assert property (@(posedge clk) copied);
endmodule
`endif
