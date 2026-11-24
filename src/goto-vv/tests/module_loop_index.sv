module module_loop_index(input logic clk);
  logic phase = 0;
  always_ff @(posedge clk) phase <= 1'b1;

  wire [31:0] incoming = phase ? 32'h5a31d7b2 : 32'h89abcdef;
  integer subtile, row, skipped;
  logic [31:0] observed, before_write, after_write;
  logic [15:0] chain;

  always @(*) begin
    observed = 32'h11223344;
    before_write = '0;
    after_write = '0;
    chain = '0;
    for (subtile = 0; subtile < 2; subtile = subtile + 1) begin
      for (row = 0; row < 2; row++) begin
        before_write[(2*subtile + row)*8 +: 8] = observed[(2*subtile + row)*8 +: 8];
        observed[(2*subtile + row)*8 +: 8] = incoming[(2*(1 - subtile) + row)*8 +: 8];
        after_write[(2*subtile + row)*8 +: 8] = observed[(2*subtile + row)*8 +: 8];
        chain[8*subtile +: 8] = row == 0 ? incoming[8*subtile +: 8]
                                       : chain[8*subtile +: 8] ^ 8'hf0;
      end
    end
    for (skipped = 4; skipped < 4; skipped++)
      observed = '0;
  end

  p_first: assert property (@(posedge clk) !phase |-> observed == 32'hcdef89ab);
  p_second: assert property (@(posedge clk) phase |-> observed == 32'hd7b25a31);
  p_before: assert property (@(posedge clk) before_write == 32'h11223344);
  p_after: assert property (@(posedge clk) after_write == observed);
  p_order: assert property (@(posedge clk) chain == (phase ? 16'h2742 : 16'h3d1f));
  p_exit: assert property (@(posedge clk) subtile == 2 && row == 2 && skipped == 4);
  p_wrong: assert property (@(posedge clk) phase |-> observed != 32'hd7b25a31);
endmodule
