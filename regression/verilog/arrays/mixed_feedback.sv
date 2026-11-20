module main(input clk);
  logic [1:0] q[0:2];
  initial begin
    q[2] = q[0];
    q[0] = 2'b01;
  end
  always @(posedge clk) q[0] <= q[1];
  assign q[1] = ~q[0];

  reg started = 0;
  always @(posedge clk) started <= 1;
  comb: assert final (q[1] == ~q[0]);
  reset_value: assert final (started || q[0] == 2'b01);
  toggles: assert property (@(posedge clk) 1 |=> q[0] == ~$past(q[0]));
  holds: assert property (@(posedge clk) 1 |=> q[2] == $past(q[2]));
  before_init: assert final (started || q[2] == 2'b01);
endmodule
