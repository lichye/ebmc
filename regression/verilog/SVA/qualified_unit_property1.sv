typedef enum logic [1:0] {OP_IDLE=2'd0, OP_RUN=2'd1} mode_t;

package ops_pkg;
  localparam logic [1:0] OP_IDLE = 2'd2;
endpackage

module main(input bit clk, input bit value, input mode_t mode);
  localparam logic [1:0] OP_IDLE = 2'd3;
  bit delayed;
  always_ff @(posedge clk) delayed <= value;

  property scoped_capture;
    bit captured;
    @(posedge clk) (1'b1, captured=value) |=>
      delayed == captured && $unit::OP_IDLE == 2'd0 &&
      ops_pkg::OP_IDLE == 2'd2 && OP_IDLE == 2'd3;
  endproperty

  property scoped_enum;
    @(posedge clk) mode == $unit::OP_IDLE |-> mode != $unit::OP_RUN;
  endproperty

  sequence running;
    mode == $unit::OP_RUN;
  endsequence

  property incorrect_scope;
    @(posedge clk) $unit::OP_IDLE == OP_IDLE;
  endproperty

  p_capture: assert property (scoped_capture);
  p_enum: assert property (scoped_enum);
  p_sequence: assert property (@(posedge clk) running |-> mode == 2'd1);
  p_refuted: assert property (incorrect_scope);
endmodule
