module wildcard_case(input logic clk, input logic [2:0] a);
  logic out_bit;
  always @(*) begin
    casez(a)
      3'b?01: out_bit = 1;
      3'b1??: out_bit = 1;
      3'bx?0: out_bit = 1;
      default: out_bit = 0;
    endcase
  end
  truth_table: assert property (@(posedge clk) out_bit == (a[2] || a[1:0] == 1));
  low_is_set: assert property (@(posedge clk) !a[2] |-> out_bit);
endmodule
