module constant_guard_dynamic_error(input logic clk, invalid);
  initial begin
    if (invalid)
      $error("runtime error");
  end
  p: assert property (@(posedge clk) 1'b1);
endmodule

module constant_guard_dynamic_finish(input logic clk, keep_running);
  logic ready;
  initial begin
    if (keep_running)
      ready = 1;
    else
      $finish;
  end
  p: assert property (@(posedge clk) ready);
endmodule

module constant_guard_unknown(input logic clk);
  initial begin
    if (1'bx)
      $error("guard has an unknown bit");
  end
  p: assert property (@(posedge clk) 1'b1);
endmodule
