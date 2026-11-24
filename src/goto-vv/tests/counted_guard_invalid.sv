module counted_guard_live(input logic clk, input logic [3:0][7:0] data);
  logic [3:0][7:0] observed;
  always_comb begin
    observed = '0;
    for (int i = 0; i < 4; i++)
      if (i == 0)
        observed[i] = data[i - 1];
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule

module counted_guard_dynamic(input logic clk, shift, input logic [3:0][7:0] data);
  logic [3:0][7:0] observed;
  always_comb begin
    observed = '0;
    for (int i = 0; i < 4; i++)
      if (shift || i >= 2)
        observed[i] = data[i - 2];
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule

module counted_guard_write(input logic clk, input logic [3:0][7:0] data);
  logic [3:0][7:0] observed;
  always_comb begin
    observed = '0;
    for (int i = 0; i < 4; i++) begin
      automatic int lane = i;
      if (lane++ == 0)
        observed[i] = data[lane];
    end
  end
  p: assert property (@(posedge clk) observed != 0);
endmodule
