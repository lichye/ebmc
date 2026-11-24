// Explicit flat reference for the ascending and offset axes and for nested reads.
module packed_select(input logic clk);
  logic [2:0][7:0] descending, descending_next;
  logic [23:0] ascending, ascending_next;
  logic [23:0] offset_range, offset_next;
  logic [15:0] nested;
  logic [1:0][1:0] four_state;
  typedef logic signed [3:0] signed_lane_t;
  signed_lane_t [1:0] signed_lanes;
  wire [7:0] signed_extended = signed_lanes[1];

  assign descending = 24'h123456;
  assign ascending = 24'hA65BC7;
  assign offset_range = 24'h345678;
  assign nested = 16'hA5C3;
  assign four_state = {2'b1z, 2'bx1};
  assign signed_lanes = 8'h89;
  always_comb begin
    descending_next = descending;
    ascending_next = ascending;
    offset_next = offset_range;
    descending_next[1] = 8'hA7;
    ascending_next[15:8] = 8'h3D;
    offset_next[23:16] = 8'hD9;
  end

  p_descending: assert property (@(posedge clk)
    descending[2] == 8'h12 && descending[1] == 8'h34 && descending[0] == 8'h56
    && descending_next == 24'h12A756);
  p_ascending: assert property (@(posedge clk)
    ascending[23:16] == 8'hA6 && ascending[15:8] == 8'h5B && ascending[7:0] == 8'hC7
    && ascending_next == 24'hA63DC7);
  p_offset: assert property (@(posedge clk)
    offset_range[23:16] == 8'h34 && offset_range[7:0] == 8'h78 && offset_next == 24'hD95678);
  p_nested: assert property (@(posedge clk)
    nested[15:12] == 4'hA && nested[11:8] == 4'h5 && nested[7:4] == 4'hC
    && nested[3:0] == 4'h3 && nested[10]);
  p_signed: assert property (@(posedge clk) signed_extended == 8'hF8);
  p_four_state: assert property (@(posedge clk) four_state[1][1] && four_state[0][0]);
  p_wrong: assert property (@(posedge clk) descending_next != 24'h12A756);
endmodule
