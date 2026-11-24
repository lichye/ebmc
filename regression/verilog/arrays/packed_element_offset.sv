module main(
  input wire [7:0] pos,
  input wire signed [4:0] neg,
  output wire [3:0] pos_asc, pos_desc, neg_asc, neg_desc,
  output wire [15:0] nested_read
);
  wire [5:7][3:0] asc = 12'hA5C;
  wire [7:5][3:0] desc = 12'hA5C;
  wire [-5:-2][3:0] asc_neg = 16'hA5C3;
  wire [-2:-5][3:0] desc_neg = 16'hA5C3;
  wire [1:-2][3:0] desc_mixed = 16'hA5C3;
  wire [5:4][3:2][3:0] nested = 16'hA5C3;

  assign pos_asc = asc[pos];
  assign pos_desc = desc[pos];
  assign neg_asc = asc_neg[neg];
  assign neg_desc = desc_neg[neg];
  assign nested_read = {nested[5][3], nested[5][2], nested[4][3], nested[4][2]};

`ifndef ORACLE
  a_pos: assume property (pos >= 5 && pos <= 7);
  a_neg: assume property (neg >= -5 && neg <= -2);

  p_pos_ends: assert property (
    asc[5] == 4'hA && asc[7] == 4'hC && desc[5] == 4'hC && desc[7] == 4'hA);
  p_neg_ends: assert property (
    asc_neg[-5] == 4'hA && asc_neg[-2] == 4'h3 &&
    desc_neg[-5] == 4'h3 && desc_neg[-2] == 4'hA);
  p_cross_zero: assert property (desc_mixed[-2] == 4'h3 && desc_mixed[1] == 4'hA);
  p_nested: assert property (nested_read == 16'hA5C3);
  p_pos_dynamic: assert property (
    {pos_asc, pos_desc} == (pos == 5 ? 8'hAC : pos == 6 ? 8'h55 : 8'hCA));
  p_neg_dynamic: assert property (
    {neg_asc, neg_desc} ==
      (neg == -5 ? 8'hA3 : neg == -4 ? 8'h5C : neg == -3 ? 8'hC5 : 8'h3A));
  p_pos_low_reachable: assert property (pos != 5);
  p_pos_high_reachable: assert property (pos != 7);
  p_neg_low_reachable: assert property (neg != -5);
  p_neg_high_reachable: assert property (neg != -2);
`endif
endmodule
