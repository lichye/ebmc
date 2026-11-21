module history(input bit clk, x);
  bit [1:0] age = 0;
  always_ff @(posedge clk) if (age < 2) age <= age + 1'b1;
  seed: assume property (@(posedge clk) (age >= 2) || !x);
  memory: assume property (@(posedge clk) (age < 2) || (x != $past(x, 2)));
  p: assert property (@(posedge clk) !x);
  hit: cover property (@(posedge clk) x);
endmodule
