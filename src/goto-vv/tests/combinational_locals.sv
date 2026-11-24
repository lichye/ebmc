module combinational_locals(input logic clk);
  logic [1:0] phase = 0;
  wire [3:0] mantissa = phase == 0 ? 4'd5 : phase == 1 ? 4'd14 : 4'd9;
  wire signed [4:0] shift_input = phase == 0 ? 5'sd2 : phase == 1 ? -5'sd1 : 5'sd9;
  wire negative = phase == 1;
  logic signed [7:0] fixed_out;

  always_ff @(posedge clk)
    if (phase < 2)
      phase <= phase + 2'd1;

  always_comb begin
    logic [7:0] unsigned_fixed;
    logic signed [4:0] shift_amount;
    shift_amount = shift_input;
    if (shift_amount >= 0 && shift_amount < 8)
      unsigned_fixed = {4'b0, mantissa} << shift_amount;
    else if (shift_amount < 0 && shift_amount > -4)
      unsigned_fixed = {4'b0, mantissa} >> (-shift_amount);
    else
      unsigned_fixed = 0;
    fixed_out = negative ? -$signed(unsigned_fixed) : $signed(unsigned_fixed);
  end

  p_left: assert property (@(posedge clk) phase == 0 |-> fixed_out == 8'sd20);
  p_right: assert property (@(posedge clk) phase == 1 |-> fixed_out == -8'sd7);
  p_limit: assert property (@(posedge clk) phase == 2 |-> fixed_out == 8'sd0);
  p_switch: assert property (@(posedge clk) phase == 0 |=> phase == 1 ##1 phase == 2);
  p_wrong: assert property (@(posedge clk) phase == 2 |-> fixed_out != 8'sd0);
endmodule
