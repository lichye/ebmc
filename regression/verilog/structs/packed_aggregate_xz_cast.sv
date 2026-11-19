module main(input bit a);
  typedef bit [1:0][1:0] array_t;
  typedef struct packed { bit [1:0] hi; bit [1:0] lo; } struct_t;
  wire array_t v = array_t'({a,1'bx,1'bz,1'b1});
  wire struct_t s = struct_t'({a,1'bx,1'bz,1'b1});
  p0: assert property (v[1] == {a,1'b0} && v[0] == 2'b01);
  p1: assert property (s.hi == {a,1'b0} && s.lo == 2'b01);
endmodule
