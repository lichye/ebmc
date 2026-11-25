module main(input clk);
  typedef enum logic { MODE_A = 2'b00, MODE_B = 1'b1 } mode_e;
  localparam mode_e mode = MODE_A;
  p: assert property (@(posedge clk) mode == 1'b0);
endmodule
