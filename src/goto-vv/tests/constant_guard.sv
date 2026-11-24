module constant_guard #(parameter int MLEN = 8, KLEN = 4)(input logic clk);
  logic [3:0] q;
  initial begin
    q = 1;
    if (MLEN % KLEN != 0) begin
      $error("MLEN must be a multiple of KLEN");
      $finish;
    end else begin
      q = 9;
    end
    if (MLEN == 8)
      q = q + 1;
    else
      $finish;
  end
  p_initial: assert property (@(posedge clk) q == 4'd10);
  p_wrong: assert property (@(posedge clk) q != 4'd10);
endmodule
