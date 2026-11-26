`ifdef EXPLICIT_REFERENCE
  `define FMT(x) x
  `define REFERENCE_WIDTH \
    function automatic logic [7:0] width(logic [2:0] fmt); \
      case (fmt) \
        3'd1: return 8; \
        3'd4: return 16; \
        3'd6: return 32; \
        default: return 1; \
      endcase \
    endfunction
`else
  `define FMT(x) fmt_t'(x)
  `define REFERENCE_WIDTH import format_pkg::*;
package format_pkg;
  typedef enum logic [2:0] { FMT8 = 1, FMT16 = 4, FMT32 = 6 } fmt_t;
  function automatic logic [7:0] width(fmt_t fmt);
    unique case (fmt)
      FMT8: return 8;
      FMT16: return 16;
      FMT32: return 32;
      default: begin
        // pragma translate_off
`ifndef EXPLICIT_REFERENCE
        if (fmt == fmt_t'(3'd2))
          $error("bad format %d", fmt); // error_site
        else
          $fatal(1, "bad format %d", fmt); // fatal_site
`endif
        // pragma translate_on
        return FMT8;
      end
    endcase
  endfunction

  function automatic logic [7:0] guarded_width(fmt_t fmt, logic enabled);
    if (enabled)
      return width(fmt); // inner_site
    return 101;
  endfunction

  function automatic logic [7:0] reported_value(logic [7:0] v);
    logic [7:0] saved;
    saved = v;
    // pragma translate_off
`ifndef EXPLICIT_REFERENCE
    if (v == 8'd3) $error("three"); // retained_site
`endif
    saved = saved + 9;
    // pragma translate_on
    return saved;
  endfunction
endpackage
`endif

module package_diagnostic_client(
  input logic [2:0] phase, input logic enabled,
  output logic [7:0] fatal_value, error_value
);
  `REFERENCE_WIDTH
  wire [2:0] fatal_fmt = phase == 3 ? 3'd3 : 3'd1;
  wire [2:0] error_fmt = phase == 2 ? 3'd2 : 3'd4;
  always_comb begin
    fatal_value = 77;
    error_value = 99;
    if (enabled) begin
      fatal_value = width(`FMT(fatal_fmt)); // caller_fatal
      error_value = width(`FMT(error_fmt)); // caller_error
    end
  end
endmodule

module package_diagnostics(input logic clk);
  `REFERENCE_WIDTH
  logic [2:0] phase = 0;
  always_ff @(posedge clk)
    if (phase != 7) phase <= phase + 3'd1;
  wire [7:0] fatal_value, error_value, safe_fatal, safe_error;
  package_diagnostic_client fires(phase, phase >= 1, fatal_value, error_value);
  package_diagnostic_client idle(phase, phase == 1, safe_fatal, safe_error);

  wire [2:0] fmt_idle = phase >= 4 ? 3'd6 : 3'd5;
  wire [2:0] fmt_fire = phase == 4 ? 3'd3 : 3'd1;
`ifdef EXPLICIT_REFERENCE
  wire [7:0] nested_idle = phase >= 4 ? width(fmt_idle) : 8'd101;
  wire [7:0] nested_fire = phase >= 1 ? width(fmt_fire) : 8'd101;
  wire [7:0] reported = 8'd9 + {5'd0, phase};
`else
  wire [7:0] nested_idle = guarded_width(fmt_t'(fmt_idle), phase >= 4); // nested_idle
  wire [7:0] nested_fire;
  assign nested_fire = guarded_width(fmt_t'(fmt_fire), phase >= 1); // nested_fire
  wire [7:0] reported = reported_value({5'd0, phase}); // reported_call
`endif
  logic [7:0] dead_branch;
  always_comb begin
    dead_branch = 23;
    if (phase >= 1 && phase == 0)
      dead_branch = width(`FMT(3'd3)); // dead_call
  end

  p_value: assert property (@(posedge clk)
    fatal_value == (phase == 0 ? 8'd77 : phase == 3 ? 8'd1 : 8'd8) &&
    error_value == (phase == 0 ? 8'd99 : phase == 2 ? 8'd1 : 8'd16));
  p_disabled: assert property (@(posedge clk)
    safe_fatal == (phase == 1 ? 8'd8 : 8'd77) &&
    safe_error == (phase == 1 ? 8'd16 : 8'd99) && dead_branch == 23);
  p_nested: assert property (@(posedge clk)
    nested_idle == (phase >= 4 ? 8'd32 : 8'd101) &&
    nested_fire == (phase == 0 ? 8'd101 : phase == 4 ? 8'd1 : 8'd8));
  p_retained: assert property (@(posedge clk) reported == 8'd9 + {5'd0, phase});
  p_wrong: assert property (@(posedge clk) phase == 3 |-> fatal_value == 8);
endmodule

module standalone_fatal(input logic clk);
  logic phase = 0;
  always_ff @(posedge clk) phase <= 1;
  always_comb begin
    if (phase) $fatal(1, "selected procedural fatal"); // standalone_site
  end
  p_data: assert property (@(posedge clk) phase || !phase);
endmodule

`undef FMT
`undef REFERENCE_WIDTH
