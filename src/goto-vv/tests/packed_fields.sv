module packed_fields(input bit clk, b, input bit [1:0] pad);
  typedef struct packed { bit hi; bit sink; bit [1:0] payload; } packet_t;
  packet_t received, forwarded;
  assign received = {~b, b, pad};
  assign forwarded.hi = received.hi;
  assign forwarded.sink = received.sink;
  assign forwarded.payload = received.payload;
  wire observed = forwarded.sink;
  typedef struct packed { bit only; } singleton_t;
  singleton_t single;
  assign single = b;
  singleton_ok: assert property (@(posedge clk) single.only == b);

  direct_ok: assert property (@(posedge clk) forwarded.sink == b);
  wire_ok: assert property (@(posedge clk) observed == b);
  adjacent_ok: assert property (@(posedge clk) forwarded.hi != b && forwarded.payload == pad);
  direct_wrong: assert property (@(posedge clk) forwarded.sink != b);
  wire_wrong: assert property (@(posedge clk) observed != b);
  field_one: cover property (@(posedge clk) b && forwarded.sink && observed);
  field_zero: cover property (@(posedge clk) !b && !forwarded.sink && !observed);
endmodule
