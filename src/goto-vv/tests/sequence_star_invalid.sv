module sequence_star_nonconsecutive(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##1 keep[=0:$] ##1 finish) |=> result);
endmodule

module sequence_star_goto(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##1 keep[->1] ##1 finish) |=> result);
endmodule

module sequence_star_bounded(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##1 keep[*0:2] ##1 finish) |=> result);
endmodule

module sequence_star_positive_min(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##1 keep[*1:$] ##1 finish) |=> result);
endmodule

module sequence_star_temporal(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##1 (keep ##1 keep)[*0:$] ##1 finish) |=> result);
endmodule

module sequence_star_delay(input logic clk, start, finish, result);
  p: assert property (@(posedge clk) (start ##[0:$] finish) |=> result);
endmodule

module sequence_star_ranged_delay(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##[1:2] keep[*0:$] ##1 finish) |=> result);
endmodule

module sequence_star_equal_range(input logic clk, start, keep, finish, result);
  p: assert property (@(posedge clk) (start ##1 keep[*8:8] ##1 finish) |=> result);
endmodule
