module main(input clk, input req);
  safe: assert property (@(posedge clk) 1);
  eventual: assert property (@(posedge clk) s_eventually req);
endmodule
