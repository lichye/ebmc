module ring(input bit clk, trip, phase, ack);
  bit active = 0;
  always_ff @(posedge clk) active <= active || trip;

  idle: assume property (@(posedge clk) !active |-> (!phase && !ack));
  once_active: assume property (@(posedge clk) active |-> !trip);
  launch: assume property (@(posedge clk) trip |=> (phase && !ack));
  high_to_low: assume property (@(posedge clk) active && phase |=> (!phase && ack));
  low_to_high: assume property (@(posedge clk) active && !phase |=> (phase && !ack));
  unreachable: assert property (@(posedge clk) !trip);
endmodule
