module tile_leaf(input logic [3:0] a, output wire [3:0] forwarded, unused_o);
  assign forwarded = a;
  assign unused_o = ~a;
endmodule

module open_outputs(input logic clk, input logic [3:0] a);
  wire [3:0] connected_q, connected_complement, open_q;
  tile_leaf connected(.a(a), .forwarded(connected_q), .unused_o(connected_complement));
  tile_leaf open(.a(a), .forwarded(open_q), .unused_o());

  connected_value: assert property (@(posedge clk) connected_q == a);
  connected_output: assert property (@(posedge clk) connected_complement == ~a);
  open_value: assert property (@(posedge clk) open_q == a);
  child_driver: assert property (@(posedge clk) open.unused_o == ~a);
  wrong_child: assert property (@(posedge clk) open.unused_o == a);
endmodule
