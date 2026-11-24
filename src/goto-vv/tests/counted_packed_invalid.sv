module counted_packed_dynamic(input logic clk, input logic runtime_index, input logic [2:0][7:0] data);
  logic [7:0] observed;
  always_comb begin
    observed = 0;
    for (int p = 0; p < 2; p++)
      observed = observed + data[p + runtime_index];
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule

module counted_packed_oob(input logic clk, input logic [1:0][7:0] data);
  logic [7:0] observed;
  always_comb begin
    observed = 0;
    for (int p = 0; p < 3; p++)
      observed = observed + data[p];
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule
