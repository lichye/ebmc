module main(input logic clk);
  typedef struct packed { logic [2:0] value; } inner_t;
  typedef struct packed { logic [1:0] tag; inner_t nested; } outer_t;

  outer_t sampled, connected;
  assign sampled = '{tag: 2'b01, nested: '{value: 3'b1xz}};
  wire [4:0] whole = sampled;
  wire [2:0] member = sampled.nested.value;

  wire [2:0] source = 3'b1xz;
  assign connected = '{tag: source[1:0], nested: '{value: source}};

  p_layout: assert property (@(posedge clk) whole[4:2] == 3'b011);
  p_consistent: assert property (@(posedge clk) whole[2:0] == member);
  p_connected: assert property (@(posedge clk)
    connected.nested.value == source && connected.tag == source[1:0]);
  p_x_zero: assert property (@(posedge clk) !member[1]);
  p_x_one: assert property (@(posedge clk) member[1]);
  p_z_zero: assert property (@(posedge clk) !member[0]);
  p_z_one: assert property (@(posedge clk) member[0]);
endmodule
