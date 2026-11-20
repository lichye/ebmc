module main(input clk, input a);

  reg a1, a2;

  always @(posedge clk) begin
    a1 <= a;
    a2 <= a1;
  end

  // holds: a2 follows a by two cycles
  p_hold: assert property (@(posedge clk) a |-> ##[1:3] a2);

  p_fail: assert property (@(posedge clk) a |-> ##1 a2);
endmodule
