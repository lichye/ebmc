module final_dynamic(input logic clk, trigger);
  logic [7:0] mem [0:1];
  final begin
    if (trigger)
      $writememh("final-guard-dump.hex", mem);
  end
  p: assert property (@(posedge clk) 1'b1);
endmodule

module final_unknown(input logic clk);
  final begin
    if (1'bx)
      ;
  end
  p: assert property (@(posedge clk) 1'b1);
endmodule

module final_assignment(input logic clk);
  logic value;
  final begin
    if (1'b1)
      value = 1'b1;
  end
  p: assert property (@(posedge clk) value);
endmodule
