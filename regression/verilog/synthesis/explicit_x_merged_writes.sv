// Many guarded bit writes with explicit x into vectors in one always
// construct. The commit samples the x bits of the four-state target.
module main(input clk, input logic [63:0] en, input logic [63:0] d);
  logic [63:0] v = 0;    // four-state
  bit [63:0] b = 0;      // two-state
  logic [63:0] q1 = 0, q2 = 0;
  always_ff @(posedge clk) begin
    for (int i = 0; i < 64; i++) begin
      if (en[i]) v[i] <= (i % 2 == 0) ? d[i] : 1'bx;
      if (en[i]) b[i] <= (i % 2 == 0) ? d[i] : 1'bx;
    end
  end
  always_ff @(posedge clk) begin q1 <= v; q2 <= v; end
  p_known: assert property (@(posedge clk) en[0] |=> v[0] == $past(d[0]));
  p_hold: assert property (@(posedge clk) !en[2] |=> v[2] == $past(v[2]));
  r_x_free0: assert property (@(posedge clk) en[1] |=> v[1] == 0);
  r_x_free1: assert property (@(posedge clk) en[1] |=> v[1] == 1);
  p_two_state: assert property (@(posedge clk) en[1] |=> b[1] == 0);
  p_same: assert property (@(posedge clk) q1 == q2);
  r_independent: assert property (@(posedge clk) en[1] && en[3] |=> v[1] == v[3]);
endmodule
