package modes;
  typedef enum logic { MODE_A = 1'b0, MODE_B = 1'b1 } mode_e;
  typedef enum bit { FLAG_A = 1'b0, FLAG_B = 1'b1 } flag_e;
endpackage

module main(input clk);
  import modes::*;

`ifdef BAD_INTEGRAL_ASSIGNMENT
  localparam mode_e selected = 1'b1;
`elsif BAD_OTHER_ENUM
  localparam mode_e selected = FLAG_B;
`else
  localparam mode_e selected = MODE_B;
  localparam flag_e other = FLAG_A;
  localparam mode_e narrowed = mode_e'(2'b10);

  p_mode: assert property (@(posedge clk)
    $bits(MODE_B) == 1 && selected == MODE_B && selected == 1'b1);
  p_flag: assert property (@(posedge clk)
    $bits(FLAG_A) == 1 && other == FLAG_A && other == 1'b0);
  p_cast: assert property (@(posedge clk) narrowed == MODE_A);
`endif
endmodule
