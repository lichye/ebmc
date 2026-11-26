// Reads of unpacked arrays whose element planes are
// uniform, with indices that can be X.
//   r3: size 3, 2-bit index: the size is representable (strict upper bound).
//   r4: size 4, 2-bit index: the size is not representable (top index 3).
//   rs: size 2, signed 2-bit index: -1 and -2 are out of range.
module main(input logic clk, s, input logic [1:0] i, input logic [3:0] a);
  wire [1:0] idx = s ? 2'bxx : i;
  wire signed [1:0] sidx = s ? 2'bxx : i;
  logic [8:0] arr3 [3], arr4 [4], arr2 [2];
  for(genvar k = 0; k < 4; k++) begin : g
    if(k < 3) assign arr3[k] = {5'b0, a} + k;
    assign arr4[k] = {5'b0, a} + k;
    if(k < 2) assign arr2[k] = {5'b0, a} + k;
  end
  wire [8:0] r3 = arr3[idx], r4 = arr4[idx], rs = arr2[sidx];
  // representable size: last element known; the size itself takes EBMC's
  // existing out-of-range path, which reads 0 here (not X, before and after)
  p3_last: assert property (@(posedge clk) !s && i == 2 |-> r3 == {5'b0, a} + 2);
  p3_oob:  assert property (@(posedge clk) !s && i == 3 |-> r3 == 0);
  // size above the largest index value: every index, including the top one
  p4_zero: assert property (@(posedge clk) !s && i == 0 |-> r4 == {5'b0, a});
  p4_top:  assert property (@(posedge clk) !s && i == 3 |-> r4 == {5'b0, a} + 3);
  // signed index: non-negative indices known, negative ones out of range
  ps_top:  assert property (@(posedge clk) !s && sidx == 1 |-> rs == {5'b0, a} + 1);
  ps_neg1: assert property (@(posedge clk) !s && sidx == -1 |-> rs == 0);
  ps_neg2: assert property (@(posedge clk) !s && sidx == -2 |-> rs == 0);
  // X index: a value that no element holds is reachable
  x_any:   assert property (@(posedge clk) !(s && a == 0 && r4 == 100));
  // wrong values are refuted
  bad4_top: assert property (@(posedge clk) !s && i == 3 |-> r4 == {5'b0, a} + 2);
  bad3_oob: assert property (@(posedge clk) !s && i == 3 |-> r3 == {5'b0, a} + 2);
  bads_neg: assert property (@(posedge clk) !s && sidx == -1 |-> rs == {5'b0, a} + 1);
  // a real execution is reachable
  w_real:  assert property (@(posedge clk) !(!s && i == 3 && a == 1 && r4 == 4));
endmodule
