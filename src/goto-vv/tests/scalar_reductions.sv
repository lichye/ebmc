module scalar_reductions(input logic clk, a, input logic [3:0] word);
  wire scalar_and = &a;
  wire scalar_or = |a;
  wire wide_and = &word;
  wire wide_or = |word;
  scalar_ok: assert property (@(posedge clk) scalar_and == a && scalar_or == a);
  wide_ok: assert property (@(posedge clk)
    wide_and == (word == 4'hf) && wide_or == (word != 4'h0));
endmodule
