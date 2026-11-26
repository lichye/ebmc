package diagnostic_invalid_pkg;
  int observed;
  function automatic int effect(input int v);
    observed = v;
    return v;
  endfunction
  function automatic int argument_write(input int v);
    $fatal(1, "argument writes %d", v++);
    return v;
  endfunction
  function automatic int argument_call(input int v);
    $error("argument calls %d", effect(v));
    return v;
  endfunction
  function automatic int missing_return(input int v);
    if (v != 0) return v;
    $fatal(1, "missing return");
  endfunction
  function automatic int simple(input int v);
    if (v == 0) $fatal(1, "zero");
    return v;
  endfunction
endpackage

module package_diagnostics_invalid(input logic clk, en, input int v);
  wire logic signed [31:0] q;
`ifdef ARGUMENT_WRITE
  assign q = diagnostic_invalid_pkg::argument_write(v);
`elsif ARGUMENT_CALL
  assign q = diagnostic_invalid_pkg::argument_call(v);
`elsif MISSING_RETURN
  assign q = diagnostic_invalid_pkg::missing_return(v);
`elsif CONDITIONAL_CALL
  assign q = en ? diagnostic_invalid_pkg::simple(v) : 0;
`elsif SHORT_CIRCUIT
  assign q = en && diagnostic_invalid_pkg::simple(v);
`elsif CONSTANT_POWER
  assign q = 2 ** diagnostic_invalid_pkg::simple(0);
`elsif SAMPLED_CALL
  assign q = v;
  p_sampled: assert property (@(posedge clk) diagnostic_invalid_pkg::simple(v) == v);
`elsif REPETITION_CALL
  assign q = v;
  int sum;
  always_comb begin
    sum = 0;
    for (int i = 0; i < 2; i++) begin
      sum = diagnostic_invalid_pkg::simple(v);
    end
  end
`else
  assign q = v;
`endif
  p_data: assert property (@(posedge clk) q == q);
endmodule
