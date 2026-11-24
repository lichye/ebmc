`include "shared.svh"
typedef logic [3:0] local_t;
module split_top(input logic clk);
  $unit::local_t value;
  $unit::guarded_word_t guard_value;
  logic leaf_valid;
  split_leaf u_leaf(leaf_valid);
  assign value = 4'h9;
  assign guard_value = 4'h9;
  isolation_ok: assert property (@(posedge clk)
    leaf_valid && value == guard_value);
endmodule
