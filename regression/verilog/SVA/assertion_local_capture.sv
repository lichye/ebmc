module main(input bit clk, input bit [3:0] a);
  bit [3:0] delayed;
  always_ff @(posedge clk) delayed <= a;
  property capture;
    bit [3:0] t;
    @(posedge clk) (1,t=a) |=> delayed == t;
  endproperty
  property live_read;
    bit [3:0] t;
    @(posedge clk) (1,t=a) |=> a == t;
  endproperty
  p0: assert property (capture);
  p1: assert property (live_read);
endmodule
