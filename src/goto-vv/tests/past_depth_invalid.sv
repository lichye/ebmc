`ifdef BAD_DYNAMIC
module past_depth_dynamic(input logic clk, value, input int depth);
  bad: assert property (@(posedge clk) $past(value, depth));
endmodule

`elsif BAD_GATE
module past_depth_gate(input logic clk, value, gate);
  bad: assert property (@(posedge clk) $past(value, 2, gate));
endmodule

`elsif BAD_CLOCK
module past_depth_clock(input logic clk, value);
  bad: assert property (@(posedge clk) $past(value, 2, , @(posedge clk)));
endmodule

`elsif BAD_ZERO
module past_depth_zero(input logic clk, value);
  bad: assert property (@(posedge clk) $past(value, 0));
endmodule

`elsif BAD_NEGATIVE
module past_depth_negative(input logic clk, value);
  bad: assert property (@(posedge clk) $past(value, -1));
endmodule

`elsif BAD_UNKNOWN
module past_depth_unknown(input logic clk, value);
  bad: assert property (@(posedge clk) $past(value, 4'bx001));
endmodule

`elsif BAD_EMPTY
module past_depth_empty(input logic clk, value);
  bad: assert property (@(posedge clk) $past(value, ));
endmodule
`endif
