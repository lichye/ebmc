`include "shared.svh"
module merge_top(input logic clk);
  $unit::word_t value;
  $unit::guarded_word_t expected_value;
  logic from_directory, from_file;
  dir_cell u_dir(from_directory);
  file_cell u_file(from_file);
  assign value = `UNIT_VALUE;
  assign expected_value = `EXPECTED_VALUE;
  value_ok: assert property (@(posedge clk) value == expected_value);
  libraries_ok: assert property (@(posedge clk) from_directory && from_file);
endmodule
