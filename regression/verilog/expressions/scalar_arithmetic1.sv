module main(input clk, input a, input b);

  // Arithmetic on two scalars is on one-bit vectors (1800-2017 6.9),
  // and wraps around in one bit.
  wire d_sub = a - b;
  wire d_add = a + b;
  wire d_mul = a * b;
  wire d_div = a / b;

  p_sub: assert property (d_sub == (a ^ b));
  p_add: assert property (d_add == (a ^ b));
  p_mul: assert property (d_mul == (a & b));
  p_div: assert property (b |-> d_div == a);
  w_sub: assert property (d_sub == (a & b));

  // Increment, decrement and compound assignments of one-bit registers.
  reg c_inc = 0, c_dec = 0, c_add = 0, c_sub = 0, q = 0, q_b = 0;

  always @(posedge clk) begin
    c_inc++;
    c_dec--;
    c_add += b;
    c_sub -= b;
    q <= !q;
    q_b <= q_b ^ b;
  end

  p_inc: assert property (c_inc == q);
  p_dec: assert property (c_dec == q);
  p_add_assign: assert property (c_add == q_b);
  p_sub_assign: assert property (c_sub == q_b);
  w_inc: assert property (c_inc == 0);

endmodule
