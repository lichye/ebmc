module decoder_features(input bit clk, rst, idx, input bit [2:0] d);
  typedef struct { logic [2:0] tag; logic valid; } word_t;
  word_t mem [1:0];
  always_ff @(posedge clk) begin
    if (rst) begin
      for (int i = 0; i < 2; i++)
        mem[i] <= '{tag: 3'd0, valid: 1'b0};
    end else
      mem[idx] <= '{tag: d, valid: 1'b1};
  end
  cleared: assert property (@(posedge clk) rst |=> !mem[0].valid && !mem[1].valid);
  last_write: assert property (@(posedge clk) !rst && idx |=> mem[1].valid && mem[1].tag == $past(d));
  wrong_word: assert property (@(posedge clk) !rst && idx |=> mem[0].tag == $past(d));
  write_live: cover property (@(posedge clk) !rst && idx);
endmodule
