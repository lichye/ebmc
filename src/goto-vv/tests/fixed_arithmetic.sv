module fixed_arithmetic(input logic clk, input logic [3:0] a);
  function automatic logic [3:0] eval_word(input logic [3:0] n);
    return ((n * 3) / 2) % 7;
  endfunction
  wire [3:0] rom [0:3];
  for (genvar k = 0; k < 4; k++) begin : words
    assign rom[k] = eval_word(k);
  end
  rom_ok: assert property (@(posedge clk) rom[3] == 4 && rom[2] == 3);
  signed_ok: assert property (@(posedge clk)
    a == 8 |-> ($signed(a) >>> 1) == -4'sd4);
  unsigned_ok: assert property (@(posedge clk)
    a == 8 |-> ($signed(a) >>> 1) == 4'b0100);
  unsigned_bad: assert property (@(posedge clk)
    a == 8 |-> ($signed(a) >>> 1) == 4'b1100);
endmodule
