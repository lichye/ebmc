module main(input bit a, b);
  p0: assert property (case (a) default: a; 1'b0: !a; endcase);
  p1: assert property (case (a) 1'b0: 1; default: b; endcase);
endmodule
