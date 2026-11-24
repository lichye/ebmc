module zero_replication(
  input logic clk,
  input logic [1:0] counter,
  input logic signed [1:0] signed_counter,
  input logic single_bit
);
  localparam int BUFFER_AMOUNT = 8 / 4;
  localparam int BUFFER_ADDR_WIDTH = $clog2(BUFFER_AMOUNT + 1);
  localparam int ADDR_WIDTH = $clog2(BUFFER_AMOUNT) + 1;

  wire [1:0] zero_left = {{ADDR_WIDTH-BUFFER_ADDR_WIDTH{1'b0}}, counter};
  wire [1:0] zero_right = {counter, {ADDR_WIDTH-BUFFER_ADDR_WIDTH{1'b0}}};
  wire [3:0] positive_left = {{2{1'b1}}, counter};
  wire [3:0] positive_right = {counter, {2{1'b0}}};
  wire one_left = {{ADDR_WIDTH-BUFFER_ADDR_WIDTH{1'b0}}, single_bit};
  wire one_right = {single_bit, {ADDR_WIDTH-BUFFER_ADDR_WIDTH{1'b0}}};
  wire [3:0] widened = {{ADDR_WIDTH-BUFFER_ADDR_WIDTH{1'b0}}, signed_counter};

  p_zero: assert property (@(posedge clk) zero_left == counter && zero_right == counter);
  p_positive: assert property (@(posedge clk)
    positive_left == {2'b11, counter} && positive_right == {counter, 2'b00});
  p_one: assert property (@(posedge clk) one_left == single_bit && one_right == single_bit);
  p_unsigned: assert property (@(posedge clk) widened == {2'b00, signed_counter});
  p_wrong: assert property (@(posedge clk)
    (counter == 2'b10 && signed_counter == 2'sb10 && single_bit)
    |-> zero_left != 2'b10);
endmodule
