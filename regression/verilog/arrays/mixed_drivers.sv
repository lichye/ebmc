module main(input clk, input [1:0] d, input sel);
  logic [1:0] q[3:1];
  assign q[3] = d;
  always_ff @(posedge clk) q[2] <= q[3];
  always @(posedge clk) q[1] <= q[2];
  initial q[1] = 2'd2;
  live: assert final (q[3] == d);
  stage1: assert property (@(posedge clk) 1 |=> q[2] == $past(d));
  stage2: assert property (@(posedge clk) 1 |=> q[1] == $past(q[2]));
  indexed: assert final (q[sel ? 1 : 3] == (sel ? q[1] : d));
  reg started = 0;
  always @(posedge clk) started <= 1;
  init_value: assert final (started || q[1] == 2);
  not_a_wire: assert final (q[2] == d);
endmodule
