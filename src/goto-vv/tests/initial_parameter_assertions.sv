module parameter_probe #(
  parameter int WIDTH = 2,
  parameter bit CHECK = 1,
  parameter logic [1:0] TRUTH = 2'b1x
) (input logic data, output logic stamp, output logic echoed);
  if (CHECK) begin : gen_lzc
    // pragma translate_off
    initial begin
      stamp = 1;
      p_width: assert (WIDTH > 0) else $fatal(1, "expected positive WIDTH");
      begin : nested
        p_truth: assert (TRUTH) else $fatal(1, "expected true TRUTH");
      end
      if (WIDTH > 0)
        assert (1) ; else $fatal;
      if (WIDTH < 0)
        assert (0) else $fatal;
      p_stamp: assert (stamp);
      p_live: assert (data);
    end
    assign echoed = data;
    // pragma translate_on
  end else begin : no_check
    assign stamp = 0;
    assign echoed = data;
  end
endmodule

module initial_parameter_assertions(input logic clk, input logic data);
  wire default_stamp, default_echo, override_stamp, override_echo;
  wire unchecked_stamp, unchecked_echo;
  parameter_probe defaults(data, default_stamp, default_echo);
`ifdef BAD_WIDTH
  parameter_probe #(.WIDTH(0)) overrides(data, override_stamp, override_echo);
`elsif BAD_X
  parameter_probe #(.WIDTH(3), .TRUTH(2'b0x)) overrides(data, override_stamp, override_echo);
`elsif BAD_Z
  parameter_probe #(.WIDTH(3), .TRUTH(2'b0z)) overrides(data, override_stamp, override_echo);
`else
  parameter_probe #(.WIDTH(3), .TRUTH(2'b1z)) overrides(data, override_stamp, override_echo);
`endif
  parameter_probe #(.WIDTH(0), .CHECK(0), .TRUTH(0))
    uninstantiated(data, unchecked_stamp, unchecked_echo);

  always @(posedge clk) begin
    p_wiring: assert ({default_echo, override_echo, unchecked_echo} == {3{data}});
    p_skipped: assert (!unchecked_stamp);
  end
endmodule
