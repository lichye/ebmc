module packed_range(input logic clk, ph_eff, one_d_base);
  localparam int KLEN = 4;
  wire [7:0][7:0] v1 = 64'hD831B42570495A16;
  wire [7:0][7:0] v2 = 64'h81236BAD51D402E7;
  wire [1:0][KLEN-1:0][7:0] top_segments, left_segments;

  for (genvar p = 0; p < 2; p++) begin : g_mini
    assign top_segments[p] = ph_eff ? v1[0 +: KLEN] : v1[p*KLEN +: KLEN];
    assign left_segments[p] = v2[p*KLEN +: KLEN];
  end

  wire [2:5][7:0] asc = 32'h13A959C6;
  wire [1:0][7:0] asc_up = asc[3 +: 2];
  wire [1:0][7:0] asc_down = asc[4 -: 2];
  wire [1:0][7:0] asc_simple = asc[3:4];
  logic [2:5][7:0] asc_updated;
  always_comb begin
    asc_updated = asc;
    asc_updated[3 +: 2] = 16'hD87E;
  end

  wire [7:0] flat = 8'h69;
  wire [1:0] old_flat_select = flat[one_d_base +: 2];

  p_shared: assert property (@(posedge clk)
    ph_eff |-> top_segments == 64'h70495A1670495A16);
  p_independent: assert property (@(posedge clk)
    !ph_eff |-> top_segments == 64'hD831B42570495A16);
  p_left: assert property (@(posedge clk)
    left_segments == 64'h81236BAD51D402E7
    && left_segments[1][0] == 8'hAD && left_segments[0][3] == 8'h51);
  p_ascending: assert property (@(posedge clk)
    asc_up == 16'hA959 && asc_down == asc_up && asc_simple == asc_up
    && asc_up[1] == 8'hA9 && asc_up[0] == 8'h59 && asc_updated == 32'h13D87EC6);
  p_1d: assert property (@(posedge clk)
    old_flat_select == (one_d_base ? 2'b00 : 2'b01));
  p_wrong_shared: assert property (@(posedge clk)
    ph_eff |-> top_segments != 64'h70495A1670495A16);
  p_wrong_independent: assert property (@(posedge clk)
    !ph_eff |-> top_segments != 64'hD831B42570495A16);
endmodule
