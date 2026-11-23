module producer(input logic value, output logic [1:0] lane);
  assign lane = {1'b0, value};
endmodule

module main(input logic clk, rst, value, select_lane);
  typedef struct packed { logic active; logic [1:0] data; } packet_t;
  wire [1:0][1:0] lanes;
  producer p0(value, lanes[0]);
  producer p1(!value, lanes[1]);
  wire packet_t packet = {value, lanes[select_lane]};

  // No loop can be closed within the default 49 transitions.
  logic [7:0] counter = 0;
  always @(posedge clk) counter <= counter + 1;

  goal: assert property (@(posedge clk) disable iff (rst) !packet.active);
endmodule
