// Spec 31 E9: FlooNoC floo_router.sv:143 .data_i(data_i[in][in_p]), reduced.
// Runtime reads of a two-level packed array of four-state packed structs.
// p_* must be PROVED, r_* must be REFUTED.
module packed_runtime_read_child(input logic clk, input logic [11:0] data_i, output logic [11:0] data_o);
  assign data_o = data_i;
endmodule

module packed_runtime_read #(parameter int unsigned NumInput = 3, parameter int unsigned NumVirt = 2)
  (input logic clk, input logic [1:0] vc, input logic [7:0] wide, input logic signed [3:0] neg,
   input logic one_bit);
  typedef struct packed { logic [3:0] hdr; logic [7:0] payload; } flit_t;
  localparam int unsigned NumPhysOne = 1;
  localparam int unsigned NumPhysTwo = 2;

  // known flits: hdr = port, payload = {port, channel}
  flit_t [NumInput-1:0][NumPhysOne-1:0] data_one;
  flit_t [NumInput-1:0][NumPhysTwo-1:0] data_two;
  for (genvar p = 0; p < NumInput; p++) begin : gen_data
    assign data_one[p][0] = '{hdr: 4'(p), payload: {4'(p), 4'hc}};
    for (genvar c = 0; c < NumPhysTwo; c++) begin : gen_c
      assign data_two[p][c] = '{hdr: 4'(p), payload: {4'(p), 4'(c)}};
    end
  end

  // FlooNoC: NumPhysChannels == 1 -> in_p = '0, NumPhysChannels == NumVirt -> in_p = v
  flit_t [NumInput-1:0][NumVirt-1:0] out_one, out_two;
  for (genvar in = 0; in < NumInput; in++) begin : gen_input
    for (genvar v = 0; v < NumVirt; v++) begin : gen_virt
      logic [$clog2(NumPhysOne) > 0 ? $clog2(NumPhysOne) - 1 : 0:0] in_p_one;
      logic [$clog2(NumPhysTwo) > 0 ? $clog2(NumPhysTwo) - 1 : 0:0] in_p_two;
      assign in_p_one = '0;
      assign in_p_two = v;
      packed_runtime_read_child u_one(.clk(clk), .data_i(data_one[in][in_p_one]), .data_o(out_one[in][v]));
      packed_runtime_read_child u_two(.clk(clk), .data_i(data_two[in][in_p_two]), .data_o(out_two[in][v]));
    end
  end

  // every port and virtual channel, corners and interior
  p_one: assert property (@(posedge clk)
    out_one[0][0] == 12'h00c && out_one[0][1] == 12'h00c && out_one[1][0] == 12'h11c &&
    out_one[1][1] == 12'h11c && out_one[2][0] == 12'h22c && out_one[2][1] == 12'h22c);
  p_two: assert property (@(posedge clk)
    out_two[0][0] == 12'h000 && out_two[0][1] == 12'h001 && out_two[1][0] == 12'h110 &&
    out_two[1][1] == 12'h111 && out_two[2][0] == 12'h220 && out_two[2][1] == 12'h221);

  // free one-bit index on a one-element dimension: 1 is out of range
  wire flit_t r_bit = data_one[1][one_bit];
  p_bit_in: assert property (@(posedge clk) !one_bit |-> r_bit == 12'h11c);
  r_bit_wrap: assert property (@(posedge clk) one_bit |-> r_bit == 12'h11c);
  r_bit_free: assert property (@(posedge clk) one_bit |-> r_bit != 12'h11c);

  // two-bit index on a two-element dimension: 2 and 3 are out of range
  wire flit_t r_vc = data_two[2][vc];
  p_vc_in0: assert property (@(posedge clk) vc == 0 |-> r_vc == 12'h220);
  p_vc_in1: assert property (@(posedge clk) vc == 1 |-> r_vc == 12'h221);
  r_vc_wrap2: assert property (@(posedge clk) vc == 2 |-> r_vc == 12'h220);
  r_vc_wrap3: assert property (@(posedge clk) vc == 3 |-> r_vc == 12'h221);

  // eight-bit index: 5 and 129 must not alias 1
  wire flit_t r_wide = data_two[1][wide];
  p_wide_in: assert property (@(posedge clk) wide == 1 |-> r_wide == 12'h111);
  r_wide_wrap5: assert property (@(posedge clk) wide == 5 |-> r_wide == 12'h111);
  r_wide_wrap129: assert property (@(posedge clk) wide == 129 |-> r_wide == 12'h111);

  // signed index: -1 must not alias 1, -2 must not alias 0
  wire flit_t r_neg = data_two[0][neg];
  p_neg_in: assert property (@(posedge clk) neg == 1 |-> r_neg == 12'h001);
  r_neg_wrap1: assert property (@(posedge clk) neg == -1 |-> r_neg == 12'h001);
  r_neg_wrap2: assert property (@(posedge clk) neg == -2 |-> r_neg == 12'h000);

  // explicit slice reference of the same layout (stride 12, row of 24)
  wire [NumInput*NumPhysTwo*12-1:0] flat = data_two;
  wire [11:0] r_slice = flat[(2 * NumPhysTwo + vc[0]) * 12 +: 12];
  p_slice: assert property (@(posedge clk) vc < 2 |-> r_vc == r_slice);
  p_member: assert property (@(posedge clk) vc < 2 |-> r_vc.hdr == 4'h2 && r_vc.payload[3:0] == {3'b0, vc[0]});
endmodule
