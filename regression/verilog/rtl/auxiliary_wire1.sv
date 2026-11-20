// A conditional increment in an unrolled loop refers to the previous
// value of the counter in both branches. The value of the counter is
// bound to auxiliary wires, which keeps its expression linear in the
// number of iterations instead of doubling with every iteration.
module main(input [31:0] x);

  reg [5:0] n;

  always_comb begin
    n = 0;
    for (int i = 0; i < 32; i++)
      if (x[i]) n = n + 1;
  end

  p0: assert final (n == $countones(x));

endmodule
