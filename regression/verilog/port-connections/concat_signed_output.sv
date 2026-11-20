module producer(input [5:0] data, output [5:0] q);
  assign q = data;
endmodule
module main(input [5:0] data);
  wire signed [2:0] hi, lo;
  producer p(data, {hi, lo});
  hi_ok: assert final (hi == data[5:3]);
  lo_ok: assert final (lo == data[2:0]);
endmodule
