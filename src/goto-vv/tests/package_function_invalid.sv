package invalid_pkg;
  int observed;
  function automatic int write_nonlocal(input int v);
    observed = v;
    return v;
  endfunction
  function automatic int read_nonlocal(input int v);
    return v + observed;
  endfunction
  function automatic int recursive(input int v);
    if (v <= 0) return 0;
    return recursive(v - 1);
  endfunction
  function automatic int static_local(input int v);
    static int saved;
    saved = v;
    return saved;
  endfunction
  function static int static_fn(input int v);
    return v;
  endfunction
  function automatic int output_arg(input int v, output int result);
    result = v;
    return v;
  endfunction
  function automatic int ref_arg(input int v, ref int result);
    result = v;
    return v;
  endfunction
  function automatic int diagnose(input int v);
    $warning("side effect %d", v);
    return v;
  endfunction
  function automatic int variable_xz_case(input logic [1:0] v);
    case (v)
      2'b0x: return 1;
      default: return 0;
    endcase
  endfunction
endpackage

module package_function_invalid(input logic clk, input int in_value);
  int result;
  int destination;
`ifdef BAD_WRITE
  always_comb result = invalid_pkg::write_nonlocal(in_value);
`elsif BAD_READ
  always_comb result = invalid_pkg::read_nonlocal(in_value);
`elsif BAD_RECURSION
  always_comb result = invalid_pkg::recursive(in_value);
`elsif BAD_STATIC_LOCAL
  always_comb result = invalid_pkg::static_local(in_value);
`elsif BAD_STATIC_FUNCTION
  always_comb result = invalid_pkg::static_fn(in_value);
`elsif BAD_OUTPUT
  always_comb result = invalid_pkg::output_arg(in_value, destination);
`elsif BAD_REF
  always_comb result = invalid_pkg::ref_arg(in_value, destination);
`elsif BAD_DIAGNOSTIC
  always_comb result = invalid_pkg::diagnose(in_value);
`elsif BAD_STATE_LABEL
  always_comb result = invalid_pkg::variable_xz_case(in_value[1:0]);
`endif
  p: assert property (@(posedge clk) result == 0);
endmodule
