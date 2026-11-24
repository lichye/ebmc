module module_loop_index_write(input logic clk);
  integer index;
  logic observed;
  always @(*) begin
    for (index = 0; index < 2; index++) begin
      observed = 1;
      index[0] = 1'b0;
    end
  end
  p: assert property (@(posedge clk) observed);
endmodule

module module_loop_index_break(input logic clk, leave);
  integer index;
  logic observed;
  always @(*) begin
    for (index = 0; index < 2; index++) begin
      if (leave)
        break;
      observed = 1;
    end
  end
  p: assert property (@(posedge clk) observed);
endmodule

module module_loop_index_runtime(input logic clk, input integer start);
  integer index;
  logic observed;
  always @(*) begin
    for (index = start; index < 2; index++)
      observed = 1;
  end
  p: assert property (@(posedge clk) observed);
endmodule

module module_loop_index_oob(input logic clk, input logic [1:0][7:0] data);
  integer index;
  logic [7:0] observed;
  always @(*) begin
    for (index = 0; index < 3; index++)
      observed = data[index];
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule

module module_loop_index_guard_write(input logic clk);
  integer index;
  logic observed;
  always @(*) begin
    for (index = 0; index++ < 2; index++)
      observed = 1;
  end
  p: assert property (@(posedge clk) observed);
endmodule

module module_loop_index_nested_init(input logic clk);
  integer index;
  logic observed;
  always @(*) begin
    for (index = 0; index < 2; index++)
      for (int row = index++; row < 2; row++)
        observed = 1;
  end
  p: assert property (@(posedge clk) observed);
endmodule
