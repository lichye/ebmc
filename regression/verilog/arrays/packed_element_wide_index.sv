// Reads of packed array elements with an index that is wider than the
// array's internal index type, or of different signedness. An index
// outside the declared range must not alias a valid element.
// The value read for an out-of-range index is not checked here.

typedef struct packed { bit [4:3][0:0] lanes; bit canary; } packed_t;

module main(
  input wire [3:0] pos,         // [3:5], 3 bits address the elements
  input wire signed [3:0] neg,  // [-2:-5], size 4 = 2**2
  input wire [69:0] wide,       // [3:6], size 4 = 2**2, index > 64 bits
  input packed_t payload,       // [4:3] in a struct, size 2 = 2**1
  input wire [5:0] member_idx);

  wire [3:5][3:0] pos_data = 12'h9A5;      // [3]=9 [4]=A [5]=5
  wire [-2:-5][3:0] neg_data = 16'h9A5C;   // [-2]=9 [-3]=A [-4]=5 [-5]=C
  wire [3:6][3:0] wide_data = 16'h9A5C;    // [3]=9 [4]=A [5]=5 [6]=C
  localparam bit [69:0] wide_alias = 70'd18446744073709551619; // 2**64+3

  wire [3:0] pos_read = pos_data[pos];
  wire [3:0] neg_read = neg_data[neg];
  wire [3:0] wide_read = wide_data[wide];
  wire member_read = payload.lanes[member_idx];

  // valid end points, constant and variable index
  p_pos_ends: assert property (
    pos_data[4'd3] == 4'h9 && pos_data[4'd5] == 4'h5);
  p_neg_ends: assert property (
    neg_data[-4'sd2] == 4'h9 && neg_data[-4'sd5] == 4'hC);
  p_wide_ends: assert property (
    wide_data[70'd3] == 4'h9 && wide_data[70'd6] == 4'hC);
  p_pos_dynamic: assert property (
    (pos != 3 || pos_read == 4'h9) && (pos != 5 || pos_read == 4'h5));
  p_neg_dynamic: assert property (
    (neg != -2 || neg_read == 4'h9) && (neg != -5 || neg_read == 4'hC));
  p_wide_dynamic: assert property (
    (wide != 3 || wide_read == 4'h9) && (wide != 6 || wide_read == 4'hC));
  p_member_dynamic: assert property (
    (member_idx != 3 || member_read == payload[1]) &&
    (member_idx != 4 || member_read == payload[2]));

  // Out-of-range indices whose truncation to the internal index type,
  // minus the lower bound, would hit a valid element:
  // pos 11 -> 3, neg 3 -> -5, wide 2**64+3 -> 3, member 11 -> 3.
  // Each must be REFUTED.
  p_pos_const_alias: assert property (pos_data[4'd11] == 4'h9);
  p_pos_alias: assert property (pos != 11 || pos_read == 4'h9);
  p_neg_const_alias: assert property (neg_data[4'sd3] == 4'hC);
  p_neg_alias: assert property (neg != 3 || neg_read == 4'hC);
  p_wide_const_alias: assert property (wide_data[wide_alias] == 4'h9);
  p_wide_alias: assert property (wide != wide_alias || wide_read == 4'h9);
  p_member_alias: assert property (
    member_idx != 11 || member_read == payload[1]);

  // the out-of-range indices above are reachable
  p_pos_reach: assert property (pos != 11);
  p_neg_reach: assert property (neg != 3);
  p_wide_reach: assert property (wide != wide_alias);
  p_member_reach: assert property (member_idx != 11);
endmodule
