`include "iscas89_dff.vh"
module main(input step_clock, CK, D);
  wire Q;
  dff dut(CK,Q,D);
  reg past_valid = 0;
  always @(posedge step_clock) past_valid <= 1;
  rise: assert property ((past_valid && !$past(CK) && CK) |-> Q == $past(D));
  low: assert property ((past_valid && !CK) |-> $stable(Q));
  high: assert property ((past_valid && $past(CK) && CK) |-> $stable(Q));
endmodule
