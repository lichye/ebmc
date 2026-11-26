`ifdef E17_REAL_MACRO
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

module slice_pipe(
  input clock, arst_n, in_valid,
  input [2:0] in_data,
  input [1:0] clear, load,
  output logic [0:2] valid,
  output logic [0:2][2:0] lane
);
  assign valid[0] = in_valid;
  assign lane[0] = in_data;
  for (genvar i = 0; i < 2; i++) begin : gen_input_pipeline
    localparam logic [2:0] RESET_VALUE = 3'(i + 1);
    `FFLARNC(valid[i+1], valid[i], load[i], clear[i], 1'b0, clock, arst_n)
    `FFLARNC(lane[i+1], lane[i], load[i], clear[i], RESET_VALUE, clock, arst_n)
  end
endmodule

module inverted_slice_pipe(
  input clock, reset, in_valid,
  input [2:0] in_data,
  input [1:0] clear, load,
  output [0:2] valid,
  output [0:2][2:0] lane
);
  slice_pipe pipe(.clock(clock), .arst_n(~reset), .in_valid(in_valid),
                  .in_data(in_data), .clear(clear), .load(load), .valid(valid), .lane(lane));
endmodule

module sliced(
  input clk_i, rst_ni, test_enable_i, gate_request, in_valid,
  input [2:0] in_data,
  input [1:0] clear, load, next_reset,
  output logic [0:2] valid,
  output logic [0:2][2:0] lane,
  output csr_reset, debug_reset
);
  wire parent_clock, child_clock, sync_n;
  wire parent_reset = ~(test_enable_i ? rst_ni : sync_n);
  RstSync sync(.clk_i(clk_i), .rstn_i(rst_ni), .clk_en(1'b1),
               .te(test_enable_i), .clk_o(parent_clock), .rstn_o(sync_n));
  reset_source source(.clock(parent_clock), .reset(parent_reset),
                      .next_reset(next_reset), .csr_reset(csr_reset), .debug_reset(debug_reset));
  reg child_request = 1'b0;
  always @(posedge clk_i) child_request <= gate_request;
  ClockGate child(.clk_i(parent_clock), .enable(child_request),
                  .te(test_enable_i), .clk_o(child_clock));
  inverted_slice_pipe stages(.clock(child_clock),
    .reset(test_enable_i ? ~rst_ni : csr_reset | debug_reset),
    .in_valid(in_valid), .in_data(in_data), .clear(clear), .load(load), .valid(valid), .lane(lane));

`ifndef EVENT_COMPARE
  p_input: assert property (@(posedge clk_i) valid[0] == in_valid && lane[0] == in_data);
  p_reset: assert property (@(posedge clk_i)
    (csr_reset || debug_reset) |-> !valid[1] && !valid[2] && lane[1] == 3'd1 && lane[2] == 3'd2);
  p_loaded: cover property (@(posedge clk_i) valid[1] && lane[1] == 3'd5);
  p_bad: assert property (@(posedge clk_i) !valid[1]);
`endif
endmodule

`ifndef E17_REAL_MACRO
  `undef FFLARNC
`endif
