// A write with a non-constant index after element-wise reset writes.
// The value of the array is flattened into its elements, using
// constant indices; the netlist simplifies reads of the updated
// array at these indices. All engines must agree.
module main(input clk, input rst, input i, input d);
  reg mem [1:0];
  reg i_q, d_q;
  reg written = 0;
  always @(posedge clk) begin
    if (rst) begin
      mem[0] <= 0;
      mem[1] <= 0;
    end else
      mem[i] <= d;
    i_q <= i;
    d_q <= d;
    written <= !rst;
  end
  // holds: the element that was written has the value written
  p: assert property (@(posedge clk) written -> mem[i_q] == d_q);
endmodule
