module main;
  localparam int a=2 ** (-2), b=(-1) ** (-3), c=(-1) ** (-2), d=1 ** (-3);
  p0: assert property (a==0 && b==-1 && c==1 && d==1);
  p1: assert property ($isunknown(0 ** (-2)));
  localparam bit [3:0] bits=4'b0001;
  p2: assert property (bits[(2 ** (-2)) +: 1] == 1'b1);
endmodule
