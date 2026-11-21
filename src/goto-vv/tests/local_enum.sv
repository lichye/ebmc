module local_enum(input logic clk);
  typedef enum logic [2:0] {IDLE = 3'd1, NEXT, FULL = 3'd7} state_t;
  state_t state = IDLE;
  always_ff @(posedge clk)
    if (state == IDLE)
      state <= NEXT;
    else
      state <= FULL;
  width_ok: assert property (@(posedge clk) $bits(FULL) == 3);
  values_ok: assert property (@(posedge clk) IDLE == 1 && NEXT == 2 && FULL == 7);
  not_full: assert property (@(posedge clk) state != FULL);
endmodule
