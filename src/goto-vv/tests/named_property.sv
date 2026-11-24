module named_property_child(input logic child_clk, rst, input bit [2:0] phase);
  property step_ok;
    @(posedge child_clk) disable iff (rst) (phase < 2) |=> (phase == 2);
  endproperty
  first_check: assert property (step_ok);
  second_check: assert property (step_ok());
endmodule

module named_property(input logic clk);
  bit [2:0] phase = 0;
  wire rst = phase == 0;
  always @(posedge clk)
    if (phase != 7) phase <= phase + 1'b1;

  named_property_child u(clk, rst, phase);

  property step_ok;
    @(posedge clk) disable iff (rst) (phase == 1) |=> (phase == 3);
  endproperty
  bad_check: assert property (step_ok);
`ifdef NATIVE_INLINE_REMOTE
  // The native frontend requires an inline spelling for the hierarchical reference.
  remote_check: assert property (@(posedge clk) disable iff (u.rst) (u.phase < 2) |=> (u.phase == 2));
`else
  remote_check: assert property (u.step_ok);
`endif

  if (1) begin : lane
    property step_ok;
      disable iff (rst) (phase == 1) |=> (phase == 2);
    endproperty
    inherited_check: assert property (@(posedge clk) step_ok);
  end

  reset_control: assert property (@(posedge clk) (phase < 2) |=> (phase == 2));

  property unused;
    @(negedge clk) (phase == 7);
  endproperty
endmodule
