module RstSync(input clk_i, input rstn_i, input clk_en, input te,
               output clk_o, output rstn_o);
  localparam RST_DELAY = 2;
  localparam CLK_DELAY = 2;
  logic [RST_DELAY + CLK_DELAY - 1 : 0] rst_delay_reg;
  always_ff @(posedge clk_i or negedge rstn_i) begin
    if (~rstn_i)
      rst_delay_reg <= '0;
    else
      rst_delay_reg <= {rst_delay_reg[RST_DELAY + CLK_DELAY - 2 : 0], 1'b1};
  end
  assign rstn_o = rst_delay_reg[RST_DELAY - 1];
  logic clk_en_int;
  assign clk_en_int = clk_en & rst_delay_reg[CLK_DELAY + RST_DELAY - 1];
  ClockGate icg(.clk_i(clk_i), .enable(clk_en_int), .te(te), .clk_o(clk_o));
endmodule

module ClockGate(input clk_i, input enable, input te, output clk_o);
  logic en_latch;
  always_latch begin
    if (!clk_i) begin
      en_latch = enable | te;
    end
  end
  assign clk_o = en_latch & clk_i;
endmodule
