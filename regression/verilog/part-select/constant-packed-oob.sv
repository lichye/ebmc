module vector_oob(input logic clk, input logic [3:0] data);
  logic signed [3:0] descending;
  logic [-2:1] ascending;
  bit [3:0] two_state;
  assign descending = data;
  assign ascending = data;
  assign two_state = data;
  localparam integer BAD = 3 + 2;
  localparam logic [127:0] BIG = (128'h1 << 100) | 128'h2;

  wire [3:0] fixed_above = descending[BAD:2];
  wire [3:0] fixed_below = ascending[0:3];
  wire [3:0] plus_above = descending[2 +: 4];
  wire [3:0] minus_below = ascending[3 -: 4];
  wire [2:0] entirely = descending[BIG +: 3];
  wire bit_unknown = descending[BIG];
  wire bit_zero = two_state[BIG];
  localparam logic [0:3] FOUR_STATE = 4'bz110;
  localparam logic [7:0] INDEX = 8'h50;
  wire [7:0] holes =
    {fixed_above[3:2], fixed_below[1:0], entirely, bit_unknown};
  for (genvar g = 0; g < 2; ++g) begin : lanes
    localparam integer OOB = g + 4;
    p_genvar: assert property (@(posedge clk) descending[OOB] === 1'bx);
  end

  p_known: assert property (@(posedge clk)
    fixed_above[1:0] == data[3:2] &&
    fixed_below[3:2] == data[1:0] &&
    plus_above[1:0] == data[3:2] &&
    minus_below[3:2] == data[1:0]);
  p_narrowed: assert property (@(posedge clk)
    descending[2'(BIG)] == data[2]);
  p_explicit_x: assert property (@(posedge clk)
    descending[BIG] === 1'bx &&
    descending[BIG +: 3] === 3'bxxx &&
    descending[BAD:2] === {2'bxx, data[3:2]} &&
    ascending[0:3] === {data[1:0], 2'bxx});
  p_bit_zero: assert property (@(posedge clk) bit_zero == 0);
  p_two_state_part: assert property (@(posedge clk) two_state[BIG +: 3] === 3'b000 && two_state[5:2] === {2'b00, data[3:2]});
  p_explicit_conversion: assert property (@(posedge clk)
    bit'(descending[BIG]) == 0 &&
    $unsigned(bit'(descending[BIG +: 3])) == 0);
  p_parameter: assert property (@(posedge clk)
    FOUR_STATE[-1:1] === 3'bxz1 && FOUR_STATE[1+:4] === 4'b110x &&
    descending[INDEX[7:4]] === 1'bx &&
    descending[5:-2] === {2'bxx, data, 2'bxx});
  p_oob_can_be_one: assert property (@(posedge clk)
    holes != 8'hff);
  p_oob_can_be_zero: assert property (@(posedge clk)
    holes != 0);
endmodule
