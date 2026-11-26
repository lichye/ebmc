module type_parameter_leaf #(parameter type T = logic signed [4:0]) (
  input logic clk,
  input T data,
  output T q,
  output wire [7:0] width,
  output wire negative
);
  assign width = $bits(T);
  assign negative = data < T'(0);
  always_ff @(posedge clk) q <= data;
endmodule

module type_parameter_relay #(parameter type T = logic) (
  input logic clk,
  input T data,
  output T q,
  output wire [7:0] width,
  output wire negative
);
  type_parameter_leaf #(.T(T)) leaf(clk, data, q, width, negative);
endmodule

module type_parameter(input logic clk);
  typedef struct packed {
    logic [2:0] tag;
    logic signed [3:0] payload;
    logic tail;
  } frame_t;
  typedef struct packed {
    logic [1:0] tag;
    logic signed [5:0] payload;
    logic [1:0] tail;
  } wide_t;

  wire signed [4:0] q_default;
  frame_t q_frame;
  wide_t q_wide;
  wire [7:0] default_width, frame_width, wide_width;
  wire default_negative, frame_negative, wide_negative;

  type_parameter_leaf with_default(
    clk, 5'sb10101, q_default, default_width, default_negative);
  type_parameter_leaf #(.T(frame_t)) with_override(
    clk, 8'b101_1101_0, q_frame, frame_width, frame_negative);
  type_parameter_relay #(.T(wide_t)) with_forward(
    clk, 10'b10_101101_01, q_wide, wide_width, wide_negative);

  p_widths: assert property (@(posedge clk)
    default_width == 5 && frame_width == 8 && wide_width == 10);
  p_sign: assert property (@(posedge clk)
    default_negative && !frame_negative && !wide_negative);
  p_default: assert property (@(posedge clk) 1'b1 |=> q_default == -5'sd11);
  p_frame: assert property (@(posedge clk) 1'b1 |=>
    q_frame == 8'b101_1101_0 && q_frame.tag == 3'b101 &&
    q_frame.payload == -4'sd3 && !q_frame.tail);
  p_forward: assert property (@(posedge clk) 1'b1 |=>
    q_wide == 10'b10_101101_01 && q_wide.tag == 2'b10 &&
    q_wide.payload == -6'sd19 && q_wide.tail == 2'b01);
  p_bad_default: assert property (@(posedge clk) 1'b1 |=> q_default == 5'sd11);
  p_bad_frame: assert property (@(posedge clk) 1'b1 |=> q_frame == 8'b101_1101_1);
  p_bad_forward: assert property (@(posedge clk) 1'b1 |=> q_wide == 10'b01_101101_10);
endmodule
