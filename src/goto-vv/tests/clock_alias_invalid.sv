module clock_alias_inverted(input clk, output logic q);
  wire sampled;
  assign sampled = ~clk;
  always @(posedge sampled) q <= 1;
endmodule

module gated_clk_cell(input clk, en, output clk_out);
  assign clk_out = clk & en;
endmodule

module clock_alias_logic_gate(input clk, en, output logic q);
  wire sampled;
  gated_clk_cell u(clk, en, sampled);
  always @(posedge sampled) q <= 1;
endmodule

module clock_alias_other_root(input clk, other, output logic q, r);
  wire sampled;
  assign sampled = clk;
  always @(posedge sampled) q <= 1;
  always @(posedge other) r <= 1;
endmodule

module clock_alias_multi(input clk, other, en, output logic q);
  wire sampled;
  assign sampled = clk;
  assign sampled = other & en;
  always @(posedge sampled) q <= 1;
endmodule

module clock_alias_multi_port(input clk, other, output logic q);
  wire sampled;
  clock_bridge first(clk, 1'b0, sampled);
  clock_bridge second(other, 1'b0, sampled);
  always @(posedge sampled) q <= 1;
endmodule

module clock_alias_cycle(input clk, output logic q);
  wire sampled, next_link;
  assign sampled = next_link;
  assign next_link = sampled;
  always @(posedge sampled) q <= 1;
endmodule

module clock_alias_source_not_bound(input clk, output logic q);
  wire sampled;
  always @(posedge sampled) q <= 1;
endmodule

module clock_alias_delayed(input clk, output logic q);
  wire sampled;
  assign #1 sampled = clk;
  always @(posedge sampled) q <= 1;
endmodule
