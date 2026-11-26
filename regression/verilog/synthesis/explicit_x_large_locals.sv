// Large values with explicit x in four-state variables that an always
// construct reads within the process: a function local and a module variable
// written with blocking assignments. A read into a two-state target, implicit
// or explicit, converts the x bits to 0; a four-state target samples them.
// The guarded shifts make the values larger than the auxiliary wire
// threshold, with the x bits in merged branches (sampled at the commit); the
// runtime bit writes keep them in an update that the sampling does not reach.
module main(input clk, input logic [63:0] d, input logic [5:0] k,
            input logic c);
  typedef bit [63:0] u64;
  typedef bit [15:0] u16;

  function automatic logic [63:0] f(input logic [63:0] d, input logic [5:0] k);
    logic [63:0] l;
    l = d;
    for (int i = 0; i < 64; i++)
      l[k + i[5:0]] = (i % 2 == 1) ? 1'bx : d[i];
    return l;
  endfunction

  function automatic logic [15:0] g(input logic [15:0] d, input logic [7:0] en);
    logic [15:0] l;
    l = d;
    for (int i = 0; i < 8; i++)
      if (en[i]) l = {l[14:0], 1'bx};
    return l;
  endfunction

  logic [63:0] m = 0, v = 0, v3 = 0;
  bit [63:0] b1 = 0, b2 = 0, b3 = 0;
  logic [15:0] n = 0, sv = 0, sv3 = 0;
  bit [15:0] s1 = 0, s2 = 0, s3 = 0;
  logic [3:0] w = 0;

  always_ff @(posedge clk) begin
    b1 <= f(d, k);        // implicit conversion
    b2 <= u64'(f(d, k));  // explicit conversion
    v <= f(d, k);         // four-state target
  end

  always_ff @(posedge clk) begin
    m = d;
    for (int i = 0; i < 64; i++)
      m[k + i[5:0]] = (i % 2 == 1) ? 1'bx : d[i];
    b3 <= m;
    v3 <= m;
  end

  always_ff @(posedge clk) begin
    s1 <= g(d[15:0], d[23:16]);                  // implicit conversion
    s2 <= u16'(g(d[15:0], d[23:16]));            // explicit conversion
    sv <= g(d[15:0], d[23:16]);                  // four-state target
  end

  always_ff @(posedge clk) begin
    n = d[15:0];
    for (int i = 0; i < 8; i++)
      if (d[16 + i]) n = {n[14:0], 1'bx};
    s3 <= n;
    sv3 <= n;
  end

  // the same value at two positions of a concatenation
  always_ff @(posedge clk) begin
    logic [1:0] t;
    t = c ? 2'bxx : 2'b00;
    w <= {t, t};
  end

  p_b1_x: assert property (@(posedge clk) k == 0 |=> b1[1] == 0);
  p_b1_known: assert property (@(posedge clk) k == 0 |=> b1[0] == $past(d[0]));
  p_b2_x: assert property (@(posedge clk) k == 0 |=> b2[1] == 0);
  p_b3_x: assert property (@(posedge clk) k == 0 |=> b3[1] == 0);
  p_b3_known: assert property (@(posedge clk) k == 0 |=> b3[2] == $past(d[2]));
  r_v_x: assert property (@(posedge clk) k == 0 |=> v[1] == 0);
  p_v_known: assert property (@(posedge clk) k == 0 |=> v[0] == $past(d[0]));
  r_v3_x: assert property (@(posedge clk) k == 0 |=> v3[1] == 0);
  p_s1_x: assert property (@(posedge clk) &d[23:16] |=> s1[7:0] == 0);
  p_s1_known: assert property (@(posedge clk) &d[23:16] |=> s1[15:8] == $past(d[7:0]));
  p_s2_x: assert property (@(posedge clk) &d[23:16] |=> s2[7:0] == 0);
  p_s3_x: assert property (@(posedge clk) &d[23:16] |=> s3[7:0] == 0);
  r_sv_x: assert property (@(posedge clk) &d[23:16] |=> sv[7:0] == 0);
  p_sv_known: assert property (@(posedge clk) &d[23:16] |=> sv[15:8] == $past(d[7:0]));
  r_sv3_x: assert property (@(posedge clk) &d[23:16] |=> sv3[7:0] == 0);
  r_w_independent: assert property (@(posedge clk) c |=> w[3:2] == w[1:0]);
  p_w_known: assert property (@(posedge clk) !c |=> w == 0);
endmodule
