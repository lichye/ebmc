
module main(input bit clk);
  bit signed [5:4][3:0] descending = 8'h96;
  bit signed [2:3][3:0] ascending = 8'ha3;
  bit [1:0] cycle = 0;

  always @(posedge clk) begin
    descending <= 8'h4d;
    ascending <= 8'h56;
    if (cycle != 3)
      cycle <= cycle + 1;
  end

  function automatic bit signed [11:0] widen(input bit signed [7:0] value);
    return value;
  endfunction

  width_kept: assert property (@(posedge clk)
    $bits($past(descending, 2)) == 8 && $bits($past(ascending, 2)) == 8);
  default_zero: assert property (@(posedge clk) cycle < 2 |->
    widen($past(descending, 2)) == 12'h000 &&
    widen($past(ascending, 2)) == 12'h000);
  first_sample: assert property (@(posedge clk) cycle == 2 |->
    widen($past(descending, 2)) == 12'hf96 &&
    widen($past(ascending, 2)) == 12'hfa3);
  later_sample: assert property (@(posedge clk) cycle == 3 |->
    widen($past(descending, 2)) == 12'h04d &&
    widen($past(ascending, 2)) == 12'h056);
  bad_default: assert property (@(posedge clk) cycle == 0 |->
    widen($past(descending, 2)) == 12'h001);
  bad_sample: assert property (@(posedge clk) cycle == 2 |->
    widen($past(ascending, 2)) == 12'hf3a);
endmodule
