`ifndef NATIVE_REFERENCE
package dispatch_pkg;
  typedef enum logic [1:0] {ADDMUL, DIVSQRT, NONCOMP, CONV} group_t;
  typedef enum logic [3:0] {
    FMADD, FNMSUB, ADD, MUL, DIV, SQRT, SGNJ, MINMAX,
    CMP, CLASSIFY, F2F, F2I, I2F, CPKAB, CPKCD, ADDS
  } op_t;

  function automatic group_t get_opgroup(input op_t op);
    unique case (op)
      FMADD, FNMSUB, ADD, ADDS, MUL: return ADDMUL;
      DIV, SQRT: return DIVSQRT;
      SGNJ, MINMAX, CMP, CLASSIFY: return NONCOMP;
      F2F, F2I, I2F, CPKAB, CPKCD: return CONV;
      default: return NONCOMP;
    endcase
  endfunction

  function automatic logic signed [4:0] bias(input logic [2:0] n);
    case (n)
      3'd0, 3'd3: return -5'sd16;
      3'd1, 3'd6: return 5'sd15;
      default: return -5'sd1;
    endcase
  endfunction

  // The selector can be statically four-valued even with a runtime payload.
  function automatic logic [3:0] classify_tag(input logic [1:0] key, input bit flip);
    unique case (key)
      2'b0x: return 4'd4 + 4'(flip);
      2'b0z: return 4'd6 + 4'(flip);
      2'b01: return 4'd8 + 4'(flip);
      default: return 4'd10 + 4'(flip);
    endcase
  endfunction
endpackage

package secondary_pkg;
  function automatic logic [1:0] get_opgroup(input logic [3:0] op);
    return op[1:0];
  endfunction
endpackage
`endif

`ifndef EVENT_REFERENCE
module package_function(input logic clk);
  bit [4:0] frame = 0;
  always @(posedge clk)
    if (frame < 5'd16) frame <= frame + 5'd1;

  wire [3:0] first_op = frame[3:0];
  wire [3:0] second_op = first_op ^ 4'h7;
  wire flip = frame[0];
  wire valid = !frame[3] || flip;
  wire [3:0] ready = flip ? 4'b1010 : 4'b0101;

  function automatic logic [1:0] reference_group(input logic [3:0] value);
    case (value)
      4'd0, 4'd1, 4'd2, 4'd3, 4'd15: return 2'd0;
      4'd4, 4'd5: return 2'd1;
      4'd10, 4'd11, 4'd12, 4'd13, 4'd14: return 2'd3;
      default: return 2'd2;
    endcase
  endfunction

  wire [1:0] ref_first = reference_group(first_op);
  wire [1:0] ref_second = reference_group(second_op);
  wire signed [6:0] ref_bias =
    (frame[2:0] == 0 || frame[2:0] == 3) ? -7'sd16 :
    (frame[2:0] == 1 || frame[2:0] == 6) ? 7'sd15 : -7'sd1;

`ifdef NATIVE_REFERENCE
  wire [1:0] first = ref_first, second = ref_second;
  wire accepted = valid & ready[ref_first];
  wire [1:0] secondary = second_op[1:0], folded_x = 2'd2, folded_z = 2'd2;
  wire signed [6:0] extended_bias = ref_bias;
  wire [3:0] exact_x = 4'd4 + 4'(flip), exact_z = 4'd6 + 4'(flip),
             exact_known = 4'd8 + 4'(flip), default_x = 4'd10 + 4'(flip),
             default_z = 4'd10 + 4'(flip);
  localparam int group_width = 2, bias_width = 5;
`else
  wire [1:0] first = dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(first_op));
  wire [1:0] second = dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(second_op));
  wire accepted = valid & ready[dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(first_op))];
  wire [1:0] secondary = secondary_pkg::get_opgroup(second_op);
  wire signed [6:0] extended_bias = dispatch_pkg::bias(frame[2:0]);
  wire [1:0] folded_x = dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(4'b00x0));
  wire [1:0] folded_z = dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(4'b00z0));
  wire [3:0] exact_x = dispatch_pkg::classify_tag(2'b0x, flip);
  wire [3:0] exact_z = dispatch_pkg::classify_tag(2'b0z, flip);
  wire [3:0] exact_known = dispatch_pkg::classify_tag(2'b01, flip);
  wire [3:0] default_x = dispatch_pkg::classify_tag(2'b1x, flip);
  wire [3:0] default_z = dispatch_pkg::classify_tag(2'b1z, flip);
  localparam int group_width = $bits(dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(first_op)));
  localparam int bias_width = $bits(dispatch_pkg::bias(frame[2:0]));
`endif

  wire [3:0] match_vector;
  for (genvar g = 0; g < 4; g++) begin : route
`ifdef NATIVE_REFERENCE
    assign match_vector[g] = valid & (ref_first == 2'(g));
`else
    assign match_vector[g] = valid & (dispatch_pkg::get_opgroup(dispatch_pkg::op_t'(first_op)) == dispatch_pkg::group_t'(g));
`endif
  end

  p_index: assert property (@(posedge clk) accepted == (valid & ready[ref_first]));
  p_generate: assert property (@(posedge clk) match_vector == (valid ? (4'b0001 << ref_first) : 4'b0));
  p_isolation: assert property (@(posedge clk)
    first == ref_first && second == ref_second && secondary == second_op[1:0]);
  p_width: assert property (@(posedge clk)
    group_width == 2 && bias_width == 5 && extended_bias == ref_bias);
  p_four_state: assert property (@(posedge clk)
    folded_x == 2'd2 && folded_z == 2'd2 &&
    exact_x == 4'd4 + 4'(flip) && exact_z == 4'd6 + 4'(flip) &&
    exact_known == 4'd8 + 4'(flip) &&
    default_x == 4'd10 + 4'(flip) && default_z == 4'd10 + 4'(flip));
  p_wrong: assert property (@(posedge clk) frame == 5'd4 |-> first == 2'd3);
endmodule
`endif
