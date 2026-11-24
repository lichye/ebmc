module modulo_bank_read(input logic clk);
  localparam int BANKS = 4, PARALLEL_DIM = 1;
  localparam int WIDE_BANKS = 5;
  logic [1:0] phase = 0;
  always_ff @(posedge clk)
    if (phase < 3)
      phase <= phase + 2'd1;

  wire [1:0] start = phase;
  wire [3:0][7:0] banks = 32'h875e3912;
  wire [4:0][7:0] wide_banks = 40'hcc9e572413;
  wire [31:0] wide_input =
    phase == 0 ? 32'h00000100 : phase == 1 ? 32'h00000101
    : phase == 2 ? 32'h00000311 : 32'hfffffffe;
  logic [3:0][7:0] rotated;

`ifdef NATIVE_FLAT
  wire [31:0] bits = banks;
  wire [39:0] wide_bits = wide_banks;
  wire [7:0] wide_value = wide_bits[((wide_input + 32'd3) % WIDE_BANKS)*8 +: 8];
`else
  wire [7:0] wide_value = wide_banks[(wide_input + 32'd3) % WIDE_BANKS];
`endif
  always_comb begin
    for (int j = 0; j < BANKS; j++)
`ifdef NATIVE_FLAT
      rotated[j] = bits[((start + j / PARALLEL_DIM) % BANKS)*8 +: 8];
`else
      rotated[j] = banks[(start + j / PARALLEL_DIM) % BANKS][0 +: 8];
`endif
  end

  wire [31:0] expected_rotated =
    start == 0 ? 32'h875e3912 : start == 1 ? 32'h12875e39
    : start == 2 ? 32'h3912875e : 32'h5e391287;
  wire [7:0] expected_wide =
    phase == 0 ? 8'hcc : phase == 1 ? 8'h13 : phase == 2 ? 8'h9e : 8'h24;

  p_rotate: assert property (@(posedge clk) rotated == expected_rotated);
  p_wide: assert property (@(posedge clk) wide_value == expected_wide);
  p_wrong: assert property (@(posedge clk) phase == 3 |-> wide_value != 8'h24);
endmodule
