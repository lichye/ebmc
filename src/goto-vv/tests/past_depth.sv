module past_depth(input logic clk);
  localparam int ONE = 1;
  bit [3:0] phase = 0;
  bit signed [4:0] sample_value;
  bit signed [4:0] q = 0;
  wire rst = (phase == 4'd3 || phase == 4'd4);
  assign sample_value = $signed({1'b0, phase}) - 5'sd9;

  always @(posedge clk) begin
    if (phase != 4'd12) phase <= phase + 4'd1;
    if (rst) q <= 5'sd0;
    else q <= sample_value;
  end

  implicit_one: assert property (@(posedge clk)
    $past(sample_value) == $past(sample_value, 1));

  default_one: assert property (@(posedge clk)
    phase < 4'd1 |-> $past(sample_value, 1) == 5'sd0);
  default_two: assert property (@(posedge clk)
    phase < 4'd2 |-> $past(sample_value, ONE + 1) == 5'sd0);
  default_three: assert property (@(posedge clk)
    phase < 4'd3 |-> $past(sample_value, 4'd3) == 5'sd0);
  default_bit: assert property (@(posedge clk)
    phase < 4'd3 |-> !$past(phase[0], 3));

  computed: assert property (@(posedge clk)
    phase <= 4'd6 |-> $past(sample_value + 5'sd2, 3) ==
      (phase < 4'd3 ? 5'sd0 : sample_value - 5'sd1));

  signed_one: assert property (@(posedge clk)
    phase >= 4'd1 && phase <= 4'd6 |->
      $past(sample_value, 1) == sample_value - 5'sd1 &&
      $past(sample_value, 1) < 5'sd0);
  signed_two: assert property (@(posedge clk)
    phase >= 4'd2 && phase <= 4'd6 |->
      $past(sample_value, ONE + 1) == sample_value - 5'sd2 &&
      $past(sample_value, ONE + 1) < 5'sd0);
  signed_three: assert property (@(posedge clk)
    phase >= 4'd3 && phase <= 4'd6 |->
      $past(sample_value, 4'd3) == sample_value - 5'sd3 &&
      $past(sample_value, 4'd3) < 5'sd0);

  before_reset: assert property (@(posedge clk)
    phase == 4'd2 |-> !rst && q == -5'sd8 &&
      $past(sample_value, 2) == -5'sd9);
  in_reset: assert property (@(posedge clk)
    phase == 4'd4 |-> rst && q == 5'sd0 &&
      $past(q, 1) == -5'sd7 && $past(sample_value, 1) == -5'sd6);
  after_reset: assert property (@(posedge clk) disable iff (rst)
    phase == 4'd5 |-> q == 5'sd0 && $past(q, 1) == 5'sd0 &&
      $past(q, 3) == -5'sd8 &&
      $past(sample_value, 1) == -5'sd5 &&
      $past(sample_value, 2) == -5'sd6 &&
      $past(sample_value, 3) == -5'sd7);
  capture_resumed: assert property (@(posedge clk)
    phase == 4'd6 |-> q == -5'sd4 && $past(q, 1) == 5'sd0);

  bad_one: assert property (@(posedge clk)
    phase >= 4'd1 |-> $past(sample_value, 1) >= 5'sd0);
  bad_two: assert property (@(posedge clk)
    phase >= 4'd2 |-> $past(sample_value, 2) >= 5'sd0);
  bad_three: assert property (@(posedge clk)
    phase >= 4'd3 |-> $past(sample_value, 3) >= 5'sd0);
  bad_after_reset: assert property (@(posedge clk) disable iff (rst)
    phase >= 4'd3 |-> $past(sample_value, 2) != -5'sd6);
endmodule
