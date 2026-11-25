module child(input [3:0] a, b, output [3:0] y);
  assign y = a - b;
endmodule

module main(input clk, input [3:0] a, b);
  wire [3:0] named_y, ordered_y, implicit_y;
  child named (
    (* async *) .b(b),
    (* latency = 2 *) (* keep *) .a(a),
    (* observe *) .y(named_y)
  );
  child ordered ((* async *) a, b, (* observe *) ordered_y);
  child implicit_named ((* async *) .a, .b, .y(implicit_y));

  wire [3:0] expected_y = a - b;
  p_named: assert property (@(posedge clk) named_y == expected_y);
  p_ordered: assert property (@(posedge clk) ordered_y == expected_y);
  p_implicit: assert property (@(posedge clk) implicit_y == expected_y);
endmodule
