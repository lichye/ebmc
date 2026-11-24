module modulo_bank_read_signed(input logic clk, input logic signed [31:0] address);
  wire [3:0][7:0] data = 32'hd9a7c54b;
  wire [7:0] observed = data[address % 32'sd4];
  p: assert property (@(posedge clk) observed != 0);
endmodule

module modulo_bank_read_zero(input logic clk, input logic [31:0] address);
  wire [3:0][7:0] data = 32'hd9a7c54b;
  wire [7:0] observed = data[address % 32'd0];
  p: assert property (@(posedge clk) observed != 0);
endmodule

module modulo_bank_read_dynamic(input logic clk, input logic [31:0] address, divisor);
  wire [3:0][7:0] data = 32'hd9a7c54b;
  wire [7:0] observed = data[address % divisor];
  p: assert property (@(posedge clk) observed != 0);
endmodule

module modulo_bank_read_wrong_count(input logic clk, input logic [31:0] address);
  wire [3:0][7:0] data = 32'hd9a7c54b;
  wire [7:0] observed = data[address % 32'd5];
  p: assert property (@(posedge clk) observed != 0);
endmodule

module modulo_bank_read_nonzero_base(input logic clk, input logic [31:0] address);
  wire [4:1][7:0] data = 32'hd9a7c54b;
  wire [7:0] observed = data[address % 32'd4];
  p: assert property (@(posedge clk) observed != 0);
endmodule
