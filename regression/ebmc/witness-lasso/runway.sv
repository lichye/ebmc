module runway(input bit clk, trip, lease);
  bit [3:0] used = 0;
  always_ff @(posedge clk)
    if (!lease) used <= 0;
    else if (used < 15) used <= used + 1'b1;

  start: assume property (@(posedge clk) trip |=> lease);
  renew: assume property (@(posedge clk) lease |=> lease);
  capacity: assume property (@(posedge clk) lease |-> used < 15);
  unreachable: assert property (@(posedge clk) !trip);
endmodule
