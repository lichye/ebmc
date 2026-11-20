module main(input clk, rst, req, ack);
  p: assert property (@(posedge clk) disable iff (rst)
    req |-> ##[1:$] ack);
endmodule
