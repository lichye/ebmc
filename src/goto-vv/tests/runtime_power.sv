module runtime_power(
  input logic clk,
  input logic [2:0] format,
  input logic [69:0] exponent,
  input logic signed [69:0] signed_exponent,
  input logic signed [2:0] short_exponent,
  input logic [6:0] context_exponent,
  output logic [31:0] bias_value,
  output logic [7:0] unsigned_value, signed_value, short_value,
  output logic signed [7:0] signed_result,
  output logic [39:0] wide_value, extended_value,
  output logic [3:0] clipped_value,
  output logic [7:0] high_value
);
  logic [2:0] phase = 0;
  always_ff @(posedge clk)
    if (phase != 7)
      phase <= phase + 3'd1;

  function automatic int unsigned exp_bits(input logic [2:0] fmt);
    case (fmt)
      0: return 8;
      1: return 11;
      2, 3: return 5;
      4: return 8;
      default: return 5;
    endcase
  endfunction

  function automatic logic [7:0] table8(input logic [69:0] n);
    case (n)
      0: return 8'h01;
      1: return 8'h02;
      2: return 8'h04;
      3: return 8'h08;
      4: return 8'h10;
      5: return 8'h20;
      6: return 8'h40;
      7: return 8'h80;
      default: return 0;
    endcase
  endfunction

  function automatic int unsigned bias(input logic [2:0] fmt);
`ifdef EXPLICIT_REFERENCE
    return unsigned'((32'd1 << (exp_bits(fmt) - 1)) - 1);
`else
    return unsigned'(2 ** (exp_bits(fmt) - 1) - 1);
`endif
  endfunction

  wire [69:0] high_count = phase == 3 ? {1'b1, 69'd2} : 70'd2;
  assign bias_value = bias(format);

`ifdef EXPLICIT_REFERENCE
  assign unsigned_value = table8(exponent);
  assign signed_value = signed_exponent < 0 ? 8'd0 : table8(signed_exponent);
  assign short_value = short_exponent < 0 ? 8'd0 : table8({67'd0, short_exponent});
  assign signed_result = 8'sd1 << context_exponent;
  assign wide_value = 40'd1 << context_exponent;
  assign extended_value = 8'(8'd1 << context_exponent);
  assign clipped_value = 4'(8'd1 << context_exponent);
  assign high_value = table8(high_count);
`else
  localparam logic [7:0] TWO = 8'd2;
  assign unsigned_value = TWO ** exponent;
  assign signed_value = 8'd2 ** signed_exponent;
  assign short_value = 8'd2 ** short_exponent;
  assign signed_result = 8'sd2 ** context_exponent;
  assign wide_value = 8'd2 ** context_exponent;
  assign extended_value = 8'(8'd2 ** context_exponent);
  assign clipped_value = 4'(8'd2 ** context_exponent);
  assign high_value = 8'd2 ** high_count;
`endif

`ifndef EVENT_SIMULATION
  p_bias: assert property (@(posedge clk)
    bias_value == (format == 0 || format == 4 ? 32'd127 :
                   format == 1 ? 32'd1023 : 32'd15));
  p_unsigned: assert property (@(posedge clk) unsigned_value == table8(exponent));
  p_signed: assert property (@(posedge clk)
    signed_value == (signed_exponent < 0 ? 8'd0 : table8(signed_exponent)));
  p_short: assert property (@(posedge clk)
    short_value == (short_exponent < 0 ? 8'd0 : table8({67'd0, short_exponent})));
  p_signed_result: assert property (@(posedge clk)
    signed_result == table8({63'd0, context_exponent}) &&
    (signed_result < 0) == (context_exponent == 7));
  p_context: assert property (@(posedge clk)
    wide_value == (40'd1 << context_exponent) &&
    extended_value == {32'd0, table8({63'd0, context_exponent})} &&
    clipped_value == 4'(8'd1 << context_exponent));
  p_high: assert property (@(posedge clk) high_value == (phase == 3 ? 8'd0 : 8'd4));
  p_wrong: assert property (@(posedge clk) phase == 3 |-> high_value == 4);
`endif
endmodule

module runtime_power_x(input logic clk, sel,
  output logic [7:0] x_value, z_value, known_value, constant_x,
  output logic [39:0] extended_x);
  // All-X results must survive even when a known high bit is already large.
  wire [69:0] x_distance = {1'b1, 67'd0, (sel ? 1'bx : 1'b1), 1'b0};
  wire signed [3:0] z_distance = {1'bz, 3'd1};
  wire [69:0] known_distance = {67'd0, (sel ? 3'd0 : 3'd7)};
`ifdef EXPLICIT_REFERENCE
  assign x_value = sel ? 8'bxxxxxxxx : 8'h00;
  assign z_value = 8'bxxxxxxxx;
  assign known_value = sel ? 8'h01 : 8'h80;
  assign constant_x = 8'bxxxxxxxx;
  assign extended_x = sel ? {32'd0, 8'bxxxxxxxx} : 40'd0;
`else
  // The known high bit alone makes the result zero only when the rest is known.
  assign x_value = 8'd2 ** x_distance;
  assign z_value = 8'd2 ** z_distance;
  assign known_value = 8'd2 ** known_distance;
  assign constant_x = 8'd2 ** 4'b1z01;
  assign extended_x = 8'(8'd2 ** x_distance);
`endif
`ifndef EVENT_SIMULATION
  p_known: assert property (@(posedge clk) known_value == (sel ? 8'h01 : 8'h80));
  p_known_overflow: assert property (@(posedge clk) !sel |-> x_value == 0);
  p_known_extension: assert property (@(posedge clk) extended_x[39:8] == 0);
  x_not_zero: assert property (@(posedge clk) sel |-> x_value != 0);
  x_not_ones: assert property (@(posedge clk) sel |-> x_value != 8'hff);
  z_not_zero: assert property (@(posedge clk) z_value != 0);
  z_not_ones: assert property (@(posedge clk) z_value != 8'hff);
  constant_not_zero: assert property (@(posedge clk) constant_x != 0);
  constant_not_ones: assert property (@(posedge clk) constant_x != 8'hff);
  extension_not_zero: assert property (@(posedge clk) sel |-> extended_x[7:0] != 0);
  extension_not_ones: assert property (@(posedge clk) sel |-> extended_x[7:0] != 8'hff);
`endif
endmodule
