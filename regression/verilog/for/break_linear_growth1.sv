// Values assigned on a path that a conditional break or return has
// terminated are not merged into the values after the if statement
// (1800-2017 12.8). Selecting between the two paths there would make
// the expression for a counter double in size with every iteration,
// i.e., 2^64 for the loops below.
module main(input [63:0] a);

  // leading zeros, counted in a loop with a conditional break
  reg [6:0] lz_break;

  always_comb begin
    lz_break = 0;
    for (int i = 1; i <= 64; i++) begin
      if (a[64 - i])
        break;
      else
        lz_break++;
    end
  end

  // the same, with a return statement in a function
  function automatic [6:0] clz(input [63:0] x);
    reg [6:0] n;
    n = 0;
    for (int i = 63; i >= 0; i--) begin
      if (x[i])
        return n;
      n = n + 1;
    end
    return n;
  endfunction

  wire [6:0] lz_return = clz(a);

  p0: assert final ((lz_break == 64) == (a == 0));
  p1: assert final (lz_break == 64 || (a[63 - lz_break] && a >> (64 - lz_break) == 0));
  p2: assert final (lz_return == lz_break);

endmodule
