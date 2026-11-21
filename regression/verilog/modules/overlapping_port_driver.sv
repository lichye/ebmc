module child(input bit a, output wire y);
  assign y = a;
endmodule

module top(input bit clk, a, b);
  wire [1:0] v;
  assign v[0] = b;
  assign v[1] = b;  // Competes with u.y whenever a and b differ.
  child u(a, v[1]);
  p_parent: assert property (@(posedge clk) v[1] == b);
  p_child: assert property (@(posedge clk) v[1] == a);
  c_agree: cover property (@(posedge clk) a == b);
  c_disagree: cover property (@(posedge clk) a != b);
endmodule
