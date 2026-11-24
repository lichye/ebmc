module main(input clk, input req);
  eventually_req: assume property (@(posedge clk) s_eventually req);
  safe: assert property (@(posedge clk) 1);
endmodule
