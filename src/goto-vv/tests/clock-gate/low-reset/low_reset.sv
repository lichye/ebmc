`ifdef E16_REAL_MACRO
  `include "registers.svh"
`else
  `define FFLARNC(__q, __d, __load, __clear, __reset_value, __clk, __arst_n) \
    always_ff @(posedge (__clk) or negedge (__arst_n)) begin              \
      if (!__arst_n) begin                                               \
        __q <= (__reset_value);                                          \
      end else begin                                                     \
        if (__clear) begin                                               \
          __q <= (__reset_value);                                        \
        end else if (__load) begin                                       \
          __q <= (__d);                                                  \
        end                                                              \
      end                                                                \
    end
`endif

module low_ff(input clock, arst_n, clear, load, input [3:0] data, output reg [3:0] q);
  `FFLARNC(q, data, load, clear, 4'h3, clock, arst_n)
endmodule

module low_forward(input clock, arst_n, clear, load, input [3:0] data, output [3:0] q);
  low_ff ff(.clock(clock), .arst_n(arst_n), .clear(clear), .load(load), .data(data), .q(q));
endmodule

module inverted_low(input clock, reset, clear, load, input [3:0] data, output [3:0] q);
  low_forward bridge(.clock(clock), .arst_n(~reset),
                     .clear(clear), .load(load), .data(data), .q(q));
endmodule

module low_reset_gate(
  input clk_i, rst_ni, test_enable_i, gate_request, clear, load, foreign_rst,
  input [3:0] data,
  input [1:0] next_reset,
  output [3:0] q, q_ext, q_sync,
  output csr_reset, debug_reset
);
  wire parent_clock, child_clock, sync_n;
  wire parent_reset = ~(test_enable_i ? rst_ni : sync_n);
  RstSync sync(.clk_i(clk_i), .rstn_i(rst_ni), .clk_en(1'b1),
               .te(test_enable_i), .clk_o(parent_clock), .rstn_o(sync_n));
  reset_source source(.clock(parent_clock), .reset(parent_reset),
                      .next_reset(next_reset), .csr_reset(csr_reset),
                      .debug_reset(debug_reset));
  reg child_request = 1'b0;
  always @(posedge clk_i) child_request <= gate_request;
  ClockGate child(.clk_i(parent_clock), .enable(child_request),
                  .te(test_enable_i), .clk_o(child_clock));

  inverted_low core(.clock(child_clock), .reset(test_enable_i ? ~rst_ni : csr_reset | debug_reset),
                    .clear(clear), .load(load), .data(data), .q(q));
  low_forward external_ff(.clock(child_clock), .arst_n(rst_ni),
                          .clear(clear), .load(load), .data(data), .q(q_ext));
  inverted_low synchronized_ff(.clock(child_clock), .reset(parent_reset),
                               .clear(clear), .load(load), .data(data), .q(q_sync));

`ifndef EVENT_COMPARE
  bit started = 0;
  always @(posedge clk_i) started <= 1'b1;
  p_async: assert property (@(posedge clk_i) (csr_reset || debug_reset) |-> q == 4'h3);
  p_external: assert property (@(posedge clk_i) !rst_ni |-> q_ext == 4'h3);
  p_sync: assert property (@(posedge clk_i) parent_reset |-> q_sync == 4'h3);
  p_hold: assert property (@(posedge clk_i)
    (started && !$past(child_request) && !$past(csr_reset || debug_reset) &&
      !csr_reset && !debug_reset) |-> q == $past(q));
  p_loaded: cover property (@(posedge clk_i) q != 4'h3 && !csr_reset && !debug_reset);
  p_bad: assert property (@(posedge clk_i) q == 4'h3);
`endif
endmodule

`ifndef E16_REAL_MACRO
  `undef FFLARNC
`endif
