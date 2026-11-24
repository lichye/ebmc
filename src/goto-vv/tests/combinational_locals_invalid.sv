module comb_local_static_init(input logic clk, data);
  logic observed;
  always_comb begin
    static logic tmp = 0;
    tmp = data;
    observed = tmp;
  end
  p: assert property (@(posedge clk) observed == data);
endmodule

module comb_local_automatic_init(input logic clk, data);
  logic observed;
  always_comb begin
    automatic logic tmp = data;
    observed = tmp;
  end
  p: assert property (@(posedge clk) observed == data);
endmodule

module comb_local_unassigned(input logic clk, data);
  logic observed;
  always_comb begin
    logic tmp;
    observed = tmp;
    tmp = data;
  end
  p: assert property (@(posedge clk) observed == data);
endmodule

module comb_local_partial(input logic clk, take, data);
  logic observed;
  always_comb begin
    logic tmp;
    if (take)
      tmp = data;
    observed = tmp;
  end
  p: assert property (@(posedge clk) observed == data);
endmodule

module comb_local_missing(input logic clk, take, data);
  logic observed;
  always_comb begin
    logic tmp;
    if (take)
      tmp = data;
    observed = data;
  end
  p: assert property (@(posedge clk) observed == data);
endmodule
