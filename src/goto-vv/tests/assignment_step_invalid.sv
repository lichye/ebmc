module assignment_step_other_target(input logic clk);
  int other;
  logic observed;
  always @(posedge clk)
    for (int i = 0; i < 2; other = i + 1)
      observed <= 1;
  p: assert property (@(posedge clk) observed);
endmodule

module assignment_step_other_source(input logic clk);
  int i = 0;
  logic observed;
  always @(posedge clk)
    for (int i = 0; i < 2; i = assignment_step_other_source.i + 1)
      observed <= 1;
  p: assert property (@(posedge clk) observed);
endmodule

module assignment_step_nonunit(input logic clk);
  logic observed;
  always @(posedge clk)
    for (int i = 0; i < 2; i = i + 2)
      observed <= 1;
  p: assert property (@(posedge clk) observed);
endmodule

module assignment_step_nonterminating(input logic clk);
  logic observed;
  always @(posedge clk)
    for (logic [1:0] i = 0; i < 4; i = i + 1)
      observed <= 1;
  p: assert property (@(posedge clk) observed);
endmodule
