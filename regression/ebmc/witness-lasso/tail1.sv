module tail(input clk, trip, alarm);
  a1: assume property (@(posedge clk) trip |=> alarm);  // trip 之后下一拍必须 alarm
`ifndef ALLOW_ALARM
  a2: assume property (@(posedge clk) !alarm);          // 永远不 alarm
`endif
  p:  assert property (@(posedge clk) !trip);           // 所以永远不 trip
endmodule
