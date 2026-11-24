module scalar_packed_read(input logic clk, input bit [0:0] data);
  localparam int LATENCY = 1;
  wire selected = data[LATENCY - 1];
  p: assert property (@(posedge clk) selected == data[0]);
endmodule
