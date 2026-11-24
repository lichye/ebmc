module packed_bank_write_wide(input logic clk, input logic [2:0] index);
  logic [3:0][7:0] bank;
  always_comb begin
    bank = '0;
    bank[index][2 +: 3] = 3'b101;
  end
  p: assert property (@(posedge clk) bank != 0);
endmodule

module packed_bank_write_dynamic_inner(input logic clk, input logic [1:0] index,
                                      input logic [2:0] first);
  logic [3:0][7:0] bank;
  always_comb begin
    bank = '0;
    bank[index][first +: 3] = 3'b101;
  end
  p: assert property (@(posedge clk) bank != 0);
endmodule

module packed_bank_write_oob_inner(input logic clk, input logic [1:0] index);
  logic [3:0][7:0] bank;
  always_comb begin
    bank = '0;
    bank[index][6 +: 3] = 3'b101;
  end
  p: assert property (@(posedge clk) bank != 0);
endmodule
