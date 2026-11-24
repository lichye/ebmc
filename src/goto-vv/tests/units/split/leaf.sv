`include "shared.svh"
typedef logic [1:0] local_t;
module split_leaf(output logic valid);
  $unit::local_t value;
  $unit::guarded_word_t guard_value;
  assign value = 2'b10;
  assign guard_value = 4'h9;
  assign valid = value == 2'b10 && guard_value == 4'h9;
endmodule
