module main(input bit c);
  typedef enum {A,B} E;
  E result;
  always_comb result=c ? A : B;
  localparam logic [7:0] w=8'($clog2(9));
  p0: assert property ((c ? result==A : result==B) && w==4);
endmodule
