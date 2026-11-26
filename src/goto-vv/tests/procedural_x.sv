// Explicit x/z written to four-state and two-state targets (EBMC Spec 29).
// A four-state target holds an unknown value, so both of its is0/is1
// checks are refuted; a two-state target or an explicit two-state cast
// holds 0.
module procedural_x(input bit clk);
  logic r_x = 1'b0, r_z = 1'b0, r_cast_x = 1'b0, r_cast_z = 1'b0;
  logic [3:0] w_mixed = 4'b0000;
  bit b_x = 1'b0;
  logic n_x;
  assign n_x = 1'bx;
  logic mem [0:1];
  struct { logic u4; bit u2; } us;
  logic c_out;
  always_comb begin
    logic c_tmp;
    c_tmp = 1'bx;
    c_out = c_tmp;
  end
  always_ff @(posedge clk) begin
    r_x <= 1'bx;
    r_z <= 1'bz;
    r_cast_x <= bit'(1'bx);
    r_cast_z <= bit'(1'bz);
    w_mixed <= 4'b10x1;
    b_x <= 1'bx;
    mem[0] <= 1'bx;
    us.u4 <= 1'bx;
    us.u2 <= 1'bx;
  end
  bit started = 0;
  always_ff @(posedge clk) started <= 1;
  r_x_is0:    assert property (@(posedge clk) started |-> r_x == 0);
  r_x_is1:    assert property (@(posedge clk) started |-> r_x == 1);
  r_z_is0:    assert property (@(posedge clk) started |-> r_z == 0);
  r_z_is1:    assert property (@(posedge clk) started |-> r_z == 1);
  cast_x_is0: assert property (@(posedge clk) started |-> r_cast_x == 0);
  cast_x_is1: assert property (@(posedge clk) started |-> r_cast_x == 1);
  cast_z_is0: assert property (@(posedge clk) started |-> r_cast_z == 0);
  cast_z_is1: assert property (@(posedge clk) started |-> r_cast_z == 1);
  mixed_known: assert property (@(posedge clk) started |-> w_mixed[3:2] == 2'b10 && w_mixed[0] == 1);
  mixed_is0:  assert property (@(posedge clk) started |-> w_mixed[1] == 0);
  mixed_is1:  assert property (@(posedge clk) started |-> w_mixed[1] == 1);
  bit_x_is0:  assert property (@(posedge clk) started |-> b_x == 0);
  mem_is0:    assert property (@(posedge clk) started |-> mem[0] == 0);
  mem_is1:    assert property (@(posedge clk) started |-> mem[0] == 1);
  us4_is0:    assert property (@(posedge clk) started |-> us.u4 == 0);
  us4_is1:    assert property (@(posedge clk) started |-> us.u4 == 1);
  us2_is0:    assert property (@(posedge clk) started |-> us.u2 == 0);
  comb_is0:   assert property (@(posedge clk) c_out == 0);
  comb_is1:   assert property (@(posedge clk) c_out == 1);
  net_is0:    assert property (@(posedge clk) n_x == 0);
  net_is1:    assert property (@(posedge clk) n_x == 1);
endmodule
