module packed_read_wide(input logic [2:0] row, output logic [7:0] word_out);
  wire [3:0][7:0] rows = 32'h9876_5432;
  assign word_out = rows[row];
endmodule

module packed_read_gap(input logic [1:0] row, output logic [7:0] word_out);
  wire [2:0][7:0] rows = 24'h76_5432;
  assign word_out = rows[row];
endmodule

module packed_read_negative(input logic signed [1:0] row, output logic [7:0] word_out);
  wire [3:0][7:0] rows = 32'h9876_5432;
  assign word_out = rows[row];
endmodule

module packed_read_lvalue(input logic row, input logic [7:0] value, output logic [15:0] result);
  logic [1:0][7:0] rows;
  always_comb begin
    rows = '0;
    rows[row] = value;
  end
  assign result = rows;
endmodule
