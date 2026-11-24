module automatic_unassigned(input logic clk, input logic take, input logic [7:0] data);
  logic [7:0] observed;
  always_comb begin
    for (int i = 0; i < 1; i++) begin
      automatic logic [7:0] tmp;
      if (take)
        tmp = data;
      observed = tmp;
    end
  end
  p: assert property (@(posedge clk) observed == data);
endmodule

module automatic_partial(input logic clk, input logic data);
  logic [7:0] observed;
  always_comb begin
    for (int i = 0; i < 1; i++) begin
      automatic logic [7:0] tmp = 0;
      tmp[0] = data;
      observed = tmp;
    end
  end
  p: assert property (@(posedge clk) observed[0] == data);
endmodule

module automatic_dynamic(input logic clk, input logic index, input logic [1:0][7:0] data);
  logic [7:0] observed;
  always_comb begin
    for (int i = 0; i < 1; i++) begin
      automatic int k = int'(index);
      observed = data[k];
    end
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule

module automatic_oob(input logic clk, input logic [1:0][7:0] data);
  logic [7:0] observed;
  always_comb begin
    for (int i = 0; i < 2; i++) begin
      automatic int k = i + 1;
      observed = data[k];
    end
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule
