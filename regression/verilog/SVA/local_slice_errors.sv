module main(input bit clk, input bit [1:0] a);
  property dynamic_bit;
    bit [3:0] v = '0;
    @(posedge clk) (1, v[a] = 1'b1) |-> v != 0;
  endproperty

  property dynamic_part;
    bit [3:0] v = '0;
    @(posedge clk) (1, v[a +: 2] = a) |-> v != 0;
  endproperty

  property outside;
    bit [3:0] v = '0;
    @(posedge clk) (1, v[3 +: 2] = a) |-> v != 0;
  endproperty

  property partial_read;
    bit [3:0] v;
    @(posedge clk) (1, v[1:0] = a) ##1 (1, v[2] = v[3]) |-> v[2];
  endproperty

  property whole_read;
    bit [3:0] v;
    @(posedge clk) (1, v[1:0] = a) |-> v == {a, a};
  endproperty

  p_bit: assert property (dynamic_bit);
  p_part: assert property (dynamic_part);
  p_outside: assert property (outside);
  p_partial: assert property (partial_read);
  p_whole: assert property (whole_read);
endmodule
