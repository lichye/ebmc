// Spec 29: where does an explicit x lose its unknown value?
// Every p_* asserts "the bit is 0". REFUTED = x kept as an unconstrained value,
// PROVED = x became 0. c_* are the two-state controls that must stay PROVED.
module sub_out(output logic [1:0] o);
  assign o = 2'bx1;
endmodule
module sub_ff(input clk, input logic [2:0] in, output logic [2:0] q);
  always_ff @(posedge clk) q <= in;   // v_mask_reg form: port input sampled by always_ff
endmodule
module main(input clk, input rst, input logic [2:0] t, input logic sel);
  // 1 direct combinational (continuous assignment)
  wire w_comb = 1'bx;
  // 2 direct non-blocking write
  logic r_nba = 0;
  always_ff @(posedge clk) r_nba <= 1'bx;
  // 3 always_comb
  logic c_comb;
  always_comb c_comb = 1'bx;
  // 4 through a wire, then NBA
  wire w_x = 1'bx;
  logic r_wire = 0;
  always_ff @(posedge clk) r_wire <= w_x;
  // 5 through a child output port, then NBA
  wire [1:0] po;
  sub_out u_out(.o(po));
  logic r_port = 0;
  always_ff @(posedge clk) r_port <= po[1];
  // 6 x in a concatenation written by NBA (Opus2 x.sv form)
  logic [3:0] r_cat = 0;
  always_ff @(posedge clk) r_cat <= {1'b0, 1'bx, t[2:1]};
  // 7 default branch / reset branch
  logic r_case = 0, r_rst = 0;
  always_ff @(posedge clk) case(sel) 1'b1: r_case <= 1'b0; default: r_case <= 'x; endcase
  always_ff @(posedge clk) if(rst) r_rst <= 1'bx; else r_rst <= 1'b0;
  // 8 combinational value into a child input port sampled by always_ff (v_mask form)
  wire [2:0] mask_in = {1'bx, t[1:0]};
  wire [2:0] mask_q;
  sub_ff u_ff(.clk(clk), .in(mask_in), .q(mask_q));
  // 9 explicit two-state conversion stays 0 (control)
  logic r_two = 0;
  always_ff @(posedge clk) r_two <= bit'(1'bx);
  bit b_two = 0;
  always_ff @(posedge clk) b_two <= 1'bx;

  p1_comb:      assert property (@(posedge clk) w_comb == 0);
  p2_nba:       assert property (@(posedge clk) r_nba == 0);
  p3_alwayscomb:assert property (@(posedge clk) c_comb == 0);
  p4_wire_nba:  assert property (@(posedge clk) r_wire == 0);
  p5_port_nba:  assert property (@(posedge clk) r_port == 0);
  p6_concat:    assert property (@(posedge clk) r_cat[2] == 0);
  p7_default:   assert property (@(posedge clk) r_case == 0);
  p7_reset:     assert property (@(posedge clk) r_rst == 0);
  p8_port_ff:   assert property (@(posedge clk) mask_q[2] == 0);
  c9_bitcast:   assert property (@(posedge clk) r_two == 0);
  c9_twostate:  assert property (@(posedge clk) b_two == 0);
  c_known:      assert property (@(posedge clk) ##1 r_cat[1:0] == $past(t[2:1]));
endmodule
