// A combinational variable that is read before it is written depends on
// itself. The reads are non-deterministic (as for any combinational
// cycle), also when they are in the definition of an auxiliary wire.
// Treating the auxiliary wire as an ordinary wire would instead require
// y == y + k for some k in [24, 72], which is unsatisfiable, and every
// property would hold vacuously.
module main(input [23:0] c);

  reg [7:0] y;

  always_comb begin
    for (int i = 0; i < 24; i++)
      if (c[i]) y = y + 1; else y = y + 3;
  end

  // y is not determined
  p0: assert final (y != 200);

endmodule
