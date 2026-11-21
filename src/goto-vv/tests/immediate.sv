module immediate(input logic clk, input logic a);
  logic q;
  initial begin
    init_ok: assert (1);
  end
  always @(*) begin
    q = a;
    if (a) begin
      comb_ok: assert (q) else $warning("q", q);
    end
    comb_bad: assert (!q);
  end
  always @(posedge clk) begin
    if (!a) begin
      clock_ok: assert (!a);
    end
  end
endmodule
