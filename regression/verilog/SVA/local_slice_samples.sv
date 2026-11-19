module main(input bit clk, input bit [1:0] a, b);
  localparam int BASE = 4;
  bit [1:0] prev;
  always_ff @(posedge clk) prev <= a;

  property across(expected_low);
    logic [7:4] v;
    @(posedge clk) (1, v[BASE +: 2] = a) ##1 (1, v[7 -: 2] = b)
      |-> v == {b, expected_low};
  endproperty

  property overlap;
    bit [0:3] v = '1;
    @(posedge clk) (1, v[0 +: 2] = a, v[1:2] = b, v[1'b0] = v[2])
      |-> v == {b[0], b, 1'b1};
  endproperty

  property read_partial;
    bit [5:2] v;
    @(posedge clk) (1, v[3:2] = a, v[5:4] = v[3:2], v[3-1] = b[0])
      |-> v == {a, a[1], b[0]};
  endproperty

  p_saved: assert property (across(prev));
  p_live: assert property (across(a));
  p_overlap: assert property (overlap);
  p_partial: assert property (read_partial);
endmodule
