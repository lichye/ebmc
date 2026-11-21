module payload(input bit clk, rst, input bit [2:0] d, output bit [2:0] q);
  always_ff @(posedge clk) q <= rst ? 3'b101 : d;
endmodule

module top(input bit clk, rst, input bit [2:0] d);
  wire [7:0] v;
  bit [2:0] expected;
  bit started = 0;
  bit saw_child_request = 0;
  bit [2:0] prev_gap;
  assign v[7] = d[0];
  assign v[0] = ~d[1];
  payload u(clk, rst, d, v[6:4]);
  // Only [3:1] is undriven, between the child segment and the parent's bit 0.
  always_ff @(posedge clk) begin
    expected <= rst ? 3'b101 : d;
    started <= !rst;
    saw_child_request <= !rst && v[6:4] == 3'b101 && d == 3'b010;
    prev_gap <= v[3:1];
  end
  p_driven: assert property (@(posedge clk) disable iff (rst)
    {v[7:4],v[0]} == {d[0],expected,~d[1]});
  p_gap_holds: assert property (@(posedge clk) disable iff (rst)
    !started || v[3:1] == prev_gap);
  p_gap_zero: assert property (@(posedge clk) disable iff (rst) v[3:1] == 0);
  c_gap_000: cover property (@(posedge clk) !rst && v[3:1] == 3'b000);
  c_gap_111: cover property (@(posedge clk) !rst && v[3:1] == 3'b111);
  c_child_changes: cover property (@(posedge clk) !rst && saw_child_request && v[6:4] == 3'b010);
endmodule
