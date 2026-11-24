module packed_read(input logic clk);
  localparam int BLEN_W = 2;
  localparam int HEAD_W = 1;
  logic [3:0] phase = 0;
  always_ff @(posedge clk) phase <= phase + 4'd1;

  wire per_head = phase[3];
  wire [3:0] drain_row = phase;
  wire [3:0][1:0][7:0] summed = 64'h7162_5344_3526_1708;
  wire [1:0][3:0][1:0][7:0] per_bank = 128'hf1e2_d3c4_b5a6_9788_796a_5b4c_3d2e_1f10;
  wire [1:0][7:0] observed =
    per_head ? per_bank[drain_row[BLEN_W +: HEAD_W]][drain_row[BLEN_W-1:0]]
             : summed[drain_row[BLEN_W-1:0]];
  wire [15:0] reference =
    per_head ? (drain_row[2] ? (drain_row[1] ? (drain_row[0] ? 16'hf1e2 : 16'hd3c4)
                                                           : (drain_row[0] ? 16'hb5a6 : 16'h9788))
                           : (drain_row[1] ? (drain_row[0] ? 16'h796a : 16'h5b4c)
                                          : (drain_row[0] ? 16'h3d2e : 16'h1f10)))
             : (drain_row[1] ? (drain_row[0] ? 16'h7162 : 16'h5344)
                             : (drain_row[0] ? 16'h3526 : 16'h1708));

  typedef logic signed [3:0] signed_lane_t;
  wire signed_lane_t [1:0] signed_lanes = 8'h97;
  wire [7:0] signed_extended = signed_lanes[drain_row[0]];

  wire [1:0][3:0] four_state_rows = {4'b1xz1, 4'b01zx};
  wire [3:0] four_state_word = four_state_rows[drain_row[0]];
  wire [3:0] resolved;
  assign resolved = four_state_word;
  assign resolved[1] = 1'b1;

  p_rows: assert property (@(posedge clk) observed == reference);
  p_lane: assert property (@(posedge clk) observed[0] == reference[7:0]);
  p_signed: assert property (@(posedge clk)
    signed_extended == (drain_row[0] ? 8'hf9 : 8'h07));
  p_z: assert property (@(posedge clk) resolved[1]);
  p_known_bit: assert property (@(posedge clk) four_state_word[3] == drain_row[0]);
  p_x0: assert property (@(posedge clk) drain_row[0] |-> !four_state_word[2]);
  p_x1: assert property (@(posedge clk) drain_row[0] |-> four_state_word[2]);
  p_wrong: assert property (@(posedge clk) phase == 15 |-> observed != 16'hf1e2);
endmodule
