module sampled_rose(input bit clk, d_valid);
  bit [3:0] tick = 0;
  always_ff @(posedge clk) tick <= tick < 9 ? tick + 1'b1 : tick;

  bit first_high = 1;
  bit rst = 0;
  bit [7:0] payload = 8'h00;
  // An explicit x or z written to a four-state variable is an unknown but
  // fixed value (EBMC Spec 29), so a rise from or to it may or may not
  // happen. A known LSB decides $rose whatever the other bits hold.
  logic four = 1'b0;
  logic [3:0] wide_four = 4'b0000;
  logic [3:0] upper_x = 4'b0000;
  logic cast_four = 1'b1;
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

  always_ff @(posedge clk) begin
    case (tick)
      0: begin upper_x <= 4'bx0z1; cast_four <= bit'(1'bx); end
      1: begin upper_x <= 4'bzx10; cast_four <= 1'b1; end
      default: upper_x <= 4'b0000;
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
  rises_wrong: assert property (@(posedge clk) tick == 1 |-> !$rose(rst));
  lsb_rises_wrong: assert property (@(posedge clk) tick == 2 |-> !$rose(payload));

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
  zero_to_x_held: assert property (@(posedge clk) tick == 1 |-> !$rose(four));
  x_to_one_held: assert property (@(posedge clk) tick == 2 |-> !$rose(four));
  one_to_z_held: assert property (@(posedge clk) tick == 3 |-> !$rose(four));
  z_to_zero_held: assert property (@(posedge clk) tick == 4 |-> !$rose(four));
  zero_to_z_held: assert property (@(posedge clk) tick == 5 |-> !$rose(four));
  z_to_one_held: assert property (@(posedge clk) tick == 6 |-> !$rose(four));
  wide_zero_to_x_held: assert property (@(posedge clk) tick == 1 |-> !$rose(wide_four));
  wide_x_to_one_held: assert property (@(posedge clk) tick == 2 |-> !$rose(wide_four));
  wide_zero_to_z_held: assert property (@(posedge clk) tick == 4 |-> !$rose(wide_four));
  wide_z_to_one_held: assert property (@(posedge clk) tick == 5 |-> !$rose(wide_four));
  upper_x_rises: assert property (@(posedge clk) tick == 1 |-> $rose(upper_x));
  upper_x_rises_wrong: assert property (@(posedge clk) tick == 1 |-> !$rose(upper_x));
  upper_x_falls: assert property (@(posedge clk) tick == 2 |-> !$rose(upper_x));
  cast_falls: assert property (@(posedge clk) tick == 1 |-> !$rose(cast_four));
  cast_rises: assert property (@(posedge clk) tick == 2 |-> $rose(cast_four));
  cast_rises_wrong: assert property (@(posedge clk) tick == 2 |-> !$rose(cast_four));

  assume_next: assume property (@(posedge clk) $rose(rst) |=> !d_valid);
  next_blocked: cover property (@(posedge clk) tick == 2 && d_valid);
  next_live: cover property (@(posedge clk) tick == 2 && !d_valid);
  same_live: cover property (@(posedge clk) tick == 1 && d_valid);
  later_live: cover property (@(posedge clk) tick == 3 && d_valid);
endmodule
