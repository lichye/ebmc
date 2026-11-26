// One-bit arithmetic: a scalar is a one-bit vector, and the result wraps
// modulo 2 (1800-2017 11.4.3, 11.6.1).
module scalar_arithmetic(input clk, input rst, input a, input b, input en,
                         input [3:0] w, input [3:0] v,
                         input bit signed s, input bit signed t);
  wire d_sub = a - b;
  wire d_add = a + b;
  wire d_mul = a * b;
  wire d_div = a / 1'b1;
  wire d_mod = a % 1'b1;
  wire d_shl = a << b;
  wire d_shr = a >> b;
  wire d_ashr = a >>> b;
  wire d_neg = -a;
  wire d_dec = a - 1'b1;
  wire [3:0] d_wshl = w << b;
  wire [3:0] d_wshr = w >> b;
  wire [3:0] d_wsub = w - v;
  wire signed d_ssub = s - t;
  wire signed d_sneg = -s;
  // The same operations, explicitly typed: two-bit operands, truncated.
  wire [1:0] x_sub = {1'b0, a} - {1'b0, b};
  wire [1:0] x_add = {1'b0, a} + {1'b0, b};
  wire [1:0] x_mul = {1'b0, a} * {1'b0, b};
  wire [1:0] x_neg = -{1'b0, a};
  wire [1:0] x_dec = {1'b0, a} - 2'd1;

  // A Chisel queue pointer: a one-bit register counted down modulo 2.
  reg wrap;
  always @(posedge clk or posedge rst)
    if(rst)
      wrap <= 1'h0;
    else if(en)
      wrap <= wrap - 1'h1;

  // Compound assignments and increment of one-bit registers.
  reg c_sub, c_inc, c_acc, c_take;
  always @(posedge clk)
    if(rst)
    begin
      c_sub <= 1'b0;
      c_inc <= 1'b0;
      c_acc <= 1'b0;
      c_take <= 1'b0;
    end
    else
    begin
      c_sub -= 1'b1;
      c_inc++;
      c_acc += b;
      c_take -= b;
    end

  p_sub: assert property (@(posedge clk) d_sub == (a ^ b));
  p_add: assert property (@(posedge clk) d_add == (a ^ b));
  p_mul: assert property (@(posedge clk) d_mul == (a & b));
  p_div: assert property (@(posedge clk) d_div == a);
  p_mod: assert property (@(posedge clk) d_mod == 1'b0);
  p_shl: assert property (@(posedge clk) d_shl == (a & !b));
  p_shr: assert property (@(posedge clk) d_shr == (a & !b));
  p_ashr: assert property (@(posedge clk) d_ashr == (a & !b));
  p_neg: assert property (@(posedge clk) d_neg == a);
  p_dec: assert property (@(posedge clk) d_dec == !a);
  p_dec0: assert property (@(posedge clk) !a |-> d_dec == 1'b1);
  p_dec1: assert property (@(posedge clk) a |-> d_dec == 1'b0);
  p_wshl: assert property (@(posedge clk) d_wshl == (b ? {w[2:0], 1'b0} : w));
  p_wshr: assert property (@(posedge clk) d_wshr == (b ? {1'b0, w[3:1]} : w));
  p_wsub: assert property (@(posedge clk) d_wsub + v == w);
  p_wsub_zero: assert property (@(posedge clk) w == v |-> d_wsub == 4'd0);
  p_ssub: assert property (@(posedge clk) d_ssub == (s ^ t));
  p_sneg: assert property (@(posedge clk) d_sneg == s);
  p_wrap: assert property (@(posedge clk) disable iff (rst)
                           en |=> wrap == !$past(wrap));
  p_wrap_hold: assert property (@(posedge clk) disable iff (rst)
                                !en |=> wrap == $past(wrap));
  p_counters: assert property (@(posedge clk) disable iff (rst)
                               1'b1 |=> c_sub == !$past(c_sub) && c_inc == !$past(c_inc));
  p_counters_b1: assert property (@(posedge clk) disable iff (rst)
                                  b |=> c_acc == !$past(c_acc) && c_take == !$past(c_take));
  p_counters_b0: assert property (@(posedge clk) disable iff (rst)
                                  !b |=> c_acc == $past(c_acc) && c_take == $past(c_take));

  e_sub: assert property (@(posedge clk) d_sub == x_sub[0]);
  e_add: assert property (@(posedge clk) d_add == x_add[0]);
  e_mul: assert property (@(posedge clk) d_mul == x_mul[0]);
  e_neg: assert property (@(posedge clk) d_neg == x_neg[0]);
  e_dec: assert property (@(posedge clk) d_dec == x_dec[0]);

  // Wrong values: each is refuted.
  w_sub: assert property (@(posedge clk) d_sub == (a & b));
  w_dec: assert property (@(posedge clk) d_dec == a);
  w_neg: assert property (@(posedge clk) d_neg == !a);
  w_wsub: assert property (@(posedge clk) d_wsub == w);
  w_counters_b: assert property (@(posedge clk) disable iff (rst)
                                 b |=> c_acc == $past(c_acc));
  w_wrap: assert property (@(posedge clk) disable iff (rst)
                           en |=> wrap == $past(wrap));
  // Antecedent witnesses: reachable, so refuted.
  r_dec0: assert property (@(posedge clk) !(!a && d_dec));
  r_wrap: assert property (@(posedge clk) disable iff (rst) en |=> !wrap);
endmodule
