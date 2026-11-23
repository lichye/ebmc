module source(input logic [3:0] data, output wire [3:0] value);
  assign value = data;
endmodule

module main(input logic [3:0] data, output wire [5:0] observed, output wire [3:0] driven);
  logic signed [5:0] partial;
  logic [5:0] low_only;
  source u_partial(data, partial[3:0]);
  source u_low_only(data, low_only[3:0]);
  assign observed = partial;
  read_low u_read_low(low_only, driven);
endmodule

module read_low(input logic [5:0] data, output wire [3:0] value);
  assign value = data[3:0];
endmodule
