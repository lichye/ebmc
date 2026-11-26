module gated_assertion_client(
  input clk_i, sample_en,
  input [1:0] grant,
  input req, grant_in, index
);
`ifdef EXPLICIT_REFERENCE
  hot_one: assert property (@(posedge clk_i) sample_en |-> $onehot0(grant));
  gnt0: assert property (@(posedge clk_i) sample_en |-> |grant |-> grant_in);
  gnt1: assert property (@(posedge clk_i) sample_en |-> req |-> grant_in |-> |grant);
  gnt_idx: assert property (@(posedge clk_i) sample_en |-> req |-> grant_in |-> grant[index]);
`else
  hot_one: assert property (@(posedge clk_i) $onehot0(grant))
    else $fatal(1, "hot_one");
  gnt0: assert property (@(posedge clk_i) |grant |-> grant_in)
    else $fatal(1, "gnt0");
  gnt1: assert property (@(posedge clk_i) req |-> grant_in |-> |grant)
    else $fatal(1, "gnt1");
  gnt_idx: assert property (@(posedge clk_i) req |-> grant_in |-> grant[index])
    else $fatal(1, "gnt_idx");
`endif

`ifdef CROSS_CLOCK_NEXT
  unsupported: assert property (@(posedge clk_i) req |=> grant_in);
`elsif CROSS_CLOCK_DELAY
  unsupported: assert property (@(posedge clk_i) req ##1 grant_in);
`elsif CROSS_CLOCK_PAST
  unsupported: assert property (@(posedge clk_i) req |-> $past(grant_in));
`elsif CROSS_CLOCK_REPEAT
  unsupported: assert property (@(posedge clk_i) req[*2]);
`elsif GATED_ASSUME
  unsupported: assume property (@(posedge clk_i) req);
`elsif GATED_NEGEDGE
  unsupported: assert property (@(negedge clk_i) req);
`elsif GATED_EFFECT
  unsupported: assert property (@(posedge clk_i) req) else $error("different action");
`endif
endmodule

module gated_sva #(parameter int FAULT = 0)(
  input clk_i, rst_ni, test_enable_i
);
  wire parent_clock, child_clock, sync_n;
  wire parent_reset = ~(test_enable_i ? rst_ni : sync_n);
  RstSync sync(.clk_i(clk_i), .rstn_i(rst_ni), .clk_en(1'b1),
               .te(test_enable_i), .clk_o(parent_clock), .rstn_o(sync_n));

  bit [3:0] phase = 0;
  bit child_enable = 1;
  bit [1:0] grant = 0;
  always @(posedge clk_i) begin
    phase <= phase + 1'b1;
    child_enable <= phase != 6 && phase != 8;
    grant <= phase == 3 || phase == 6 || (phase == 7 && FAULT == 1) ||
             (phase == 8 && FAULT == 0) ? 2'b11 :
             phase == 5 || (phase == 7 && FAULT == 3) ? 2'b00 : 2'b01;
  end
  ClockGate child(.clk_i(parent_clock), .enable(child_enable),
                  .te(test_enable_i), .clk_o(child_clock));

  logic [3:0] reference_release;
  always @(posedge clk_i)
    if (!rst_ni) reference_release <= '0;
    else reference_release <= {reference_release[2:0], 1'b1};
  wire parent_passes = rst_ni && reference_release[3];
  wire reference_enable = parent_passes && child_enable;
  wire req = |grant || (phase == 8 && FAULT == 3);
  wire grant_in = (|grant && !(phase == 8 && FAULT == 2)) ||
                  (phase == 8 && FAULT == 3);
  wire index = phase == 8 && FAULT == 4;
`ifdef EXPLICIT_REFERENCE
  gated_assertion_client client(clk_i, reference_enable, grant, req, grant_in, index);
`else
  gated_assertion_client client(child_clock, 1'b1, grant, req, grant_in, index);
`endif

  native_onehot0_scalar: assert property (@(posedge clk_i) $onehot0(grant_in));
  native_onehot0_constant: assert property (@(posedge clk_i) $onehot0(3'b100));

`ifdef UNGUARDED_CONTROL
  root_control: assert property (@(posedge clk_i) $onehot0(grant));
`endif
`ifndef EVENT_SIMULATION
  c_parent_closed: cover property (@(posedge clk_i)
    phase == 4 && !parent_passes && child_enable && grant == 2'b11);
  c_child_closed: cover property (@(posedge clk_i)
    phase == 7 && parent_passes && !child_enable && grant == 2'b11);
  c_zero: cover property (@(posedge clk_i)
    phase == 6 && reference_enable && grant == 2'b00);
  c_first_one: cover property (@(posedge clk_i)
    phase == 8 && reference_enable && grant == 2'b01);
`endif
endmodule
