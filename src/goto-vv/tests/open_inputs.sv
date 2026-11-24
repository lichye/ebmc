module input_leaf(input wire [3:0] data, output wire [3:0] first, second);
  assign first = data;
  assign second = data;
endmodule

module default_leaf(input wire [3:0] data = 4'ha, output wire [3:0] forwarded);
  assign forwarded = data;
endmodule

module open_inputs(input logic clk, input wire [3:0] data);
  wire [3:0] omitted_first, omitted_second, empty_first, empty_second;
  wire [3:0] assigned_data, default_data, override_data, empty_default_data;

  input_leaf omitted(.first(omitted_first), .second(omitted_second));
  input_leaf explicit_empty(.data(), .first(empty_first), .second(empty_second));
  input_leaf bound(.data(data), .first(assigned_data), .second());
  default_leaf defaulted(.forwarded(default_data));
  default_leaf overridden(.data(data), .forwarded(override_data));
  default_leaf empty_default(.data(), .forwarded(empty_default_data));

  p_omitted_shared: assert property (@(posedge clk) omitted_first == omitted_second);
  p_empty_shared: assert property (@(posedge clk) empty_first == empty_second);
  p_bound: assert property (@(posedge clk) assigned_data == data);
  p_default: assert property (@(posedge clk) default_data == 4'ha);
  p_override: assert property (@(posedge clk) override_data == data);
  p_omitted_hold: assert property (@(posedge clk) omitted_first == 4'h6 |=> omitted_first == 4'h6);
  p_empty_hold: assert property (@(posedge clk) empty_first == 4'h9 |=> empty_first == 4'h9);
  p_omitted_zero: assert property (@(posedge clk) omitted_first != 4'h0);
  p_empty_ones: assert property (@(posedge clk) empty_first != 4'hf);
  p_open_default: assert property (@(posedge clk) empty_default_data == 4'ha);
  p_distinct: assert property (@(posedge clk) omitted_first == empty_first);
endmodule
