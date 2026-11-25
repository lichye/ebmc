module sampled_rose(input bit clk, d_valid);
  bit [3:0] tick = 0;
  always_ff @(posedge clk) tick <= tick < 9 ? tick + 1'b1 : tick;

  bit first_high = 1;
  bit rst = 0;
  bit [7:0] payload = 8'h00;
  logic four = 1'b0;
  logic [3:0] wide_four = 4'b0000;
  always_ff @(posedge clk) begin
    case (tick)
      0: begin rst <= 1; payload <= 8'h80; four <= 1'bx; end
      1: begin rst <= 1; payload <= 8'h81; four <= 1'b1; end
      2: begin rst <= 0; payload <= 8'h01; four <= 1'bz; end
      3: begin payload <= 8'h00; four <= 1'b0; end
      4: four <= 1'bz;
      5: four <= 1'b1;
      default: four <= 1'b0;
    endcase
  end

  always_ff @(posedge clk) begin
    case (tick)
      0: wide_four <= 4'b100x;
      1: wide_four <= 4'b1001;
      2: wide_four <= 4'b0000;
      3: wide_four <= 4'b100z;
      4: wide_four <= 4'b1001;
      default: wide_four <= 4'b0000;
    endcase
  end

  first_zero: assert property (@(posedge clk) tick == 0 |-> !$rose(rst));
  first_one: assert property (@(posedge clk) tick == 0 |-> $rose(first_high));
  localparam logic known_one = 1'b1;
  first_constant: assert property (@(posedge clk) tick == 0 |-> $rose(known_one));
  first_bit_cast: assert property (@(posedge clk) tick == 0 |-> !$rose(bit'(1'bz)));
  rises: assert property (@(posedge clk) tick == 1 |-> $rose(rst));
  holds: assert property (@(posedge clk) tick == 2 |-> !$rose(rst));
  falls: assert property (@(posedge clk) tick == 3 |-> !$rose(rst));
  upper_rises: assert property (@(posedge clk) tick == 1 |-> !$rose(payload));
  lsb_rises: assert property (@(posedge clk) tick == 2 |-> $rose(payload));
  upper_falls: assert property (@(posedge clk) tick == 3 |-> !$rose(payload));
  lsb_falls: assert property (@(posedge clk) tick == 4 |-> !$rose(payload));

  zero_to_x: assert property (@(posedge clk) tick == 1 |-> $rose(four));
  x_to_one: assert property (@(posedge clk) tick == 2 |-> $rose(four));
  one_to_z: assert property (@(posedge clk) tick == 3 |-> $rose(four));
  z_to_zero: assert property (@(posedge clk) tick == 4 |-> $rose(four));
  zero_to_z: assert property (@(posedge clk) tick == 5 |-> $rose(four));
  z_to_one: assert property (@(posedge clk) tick == 6 |-> $rose(four));
  wide_zero_to_x: assert property (@(posedge clk) tick == 1 |-> $rose(wide_four));
  wide_x_to_one: assert property (@(posedge clk) tick == 2 |-> $rose(wide_four));
  wide_zero_to_z: assert property (@(posedge clk) tick == 4 |-> $rose(wide_four));
  wide_z_to_one: assert property (@(posedge clk) tick == 5 |-> $rose(wide_four));

  assume_next: assume property (@(posedge clk) $rose(rst) |=> !d_valid);
  next_blocked: cover property (@(posedge clk) tick == 2 && d_valid);
  next_live: cover property (@(posedge clk) tick == 2 && !d_valid);
  same_live: cover property (@(posedge clk) tick == 1 && d_valid);
  later_live: cover property (@(posedge clk) tick == 3 && d_valid);
endmodule
