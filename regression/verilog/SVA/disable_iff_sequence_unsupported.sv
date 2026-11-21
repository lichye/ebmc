module top(input bit clk, rst, a, b, c);
  mixed_repetitions: assert property (@(posedge clk) disable iff (rst)
    (a ##[1:2] b)[*2] |=> c);
  unbounded_delay: assert property (@(posedge clk) disable iff (rst)
    (a ##[1:$] b) |-> c);
endmodule
