module packed_bank_write(input logic clk);
  logic [1:0] phase = 0;
  always_ff @(posedge clk)
    if (phase < 3)
      phase <= phase + 2'd1;

  wire [1:0] start = phase;
  wire [31:0] payload = 32'h875e3912;
  integer bank, row;
  logic [1:0] target;
  logic [3:0][7:0] rotated, partial;
  logic [7:0] before_write, between_writes, after_write;
  always @(*) begin
    for (bank = 0; bank < 4; bank = bank + 1) begin
      target = bank[1:0] + start;
      for (row = 0; row < 1; row++)
        rotated[target][row*8 +: 8] = payload[bank*8 +: 8];
    end
  end

  always @(*) begin
    partial = 32'ha163c4e7;
    before_write = partial[start];
    partial[start][2 +: 3] = 3'b101;
    between_writes = partial[start];
    partial[start][5 -: 2] = partial[start][3:2];
    after_write = partial[start];
  end

  wire [7:0] seed =
    start == 0 ? 8'he7 : start == 1 ? 8'hc4 : start == 2 ? 8'h63 : 8'ha1;
  wire [7:0] expected_between = {seed[7:5], 3'b101, seed[1:0]};
  wire [7:0] expected_after = {seed[7:6], expected_between[3:2], expected_between[3:0]};
  wire [31:0] expected_partial =
    start == 0 ? {24'ha163c4, expected_after}
    : start == 1 ? {16'ha163, expected_after, 8'he7}
    : start == 2 ? {8'ha1, expected_after, 16'hc4e7}
                 : {expected_after, 24'h63c4e7};

  p_rotation: assert property (@(posedge clk)
    rotated == (start == 0 ? payload
                : start == 1 ? {payload[23:0], payload[31:24]}
                : start == 2 ? {payload[15:0], payload[31:16]}
                             : {payload[7:0], payload[31:8]}));
  p_before: assert property (@(posedge clk) before_write == seed);
  p_chain: assert property (@(posedge clk)
    between_writes == expected_between && after_write == expected_after);
  p_adjacent: assert property (@(posedge clk) partial == expected_partial);
  p_wrong: assert property (@(posedge clk) phase == 3 |-> after_write != expected_after);
endmodule
