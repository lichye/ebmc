module sub(output tri1 [3:0] up, output tri0 down);
endmodule
module main(input d, e);
  wire [3:0] up;
  wire down;
  sub pulls(up,down);
  tri1 [3:0] bus;
  assign bus[2:1] = e ? {d, !d} : 2'bzz;
  bufif1 (bus[2], d, e);
  tri1 [3:0] unsized;
  assign unsized = e ? {4{d}} : 'z;
  fill_literal: assert property (!e |-> unsized == 4'hf);
  wire [1:0] partial;
  assign partial[0] = d;
  tri1 [1:0] padded = partial;
  released_bit: assert property (padded == {1'b1, d});
  defaults: assert property (up == 4'b1111 && !down);
  pulls_release: assert property (!e |-> bus == 4'b1111);
  driven_bits: assert property (e |-> bus == {1'b1,d,!d,1'b1});
endmodule
