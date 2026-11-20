module leaf #(parameter string LABEL = "") (
  input clk,
  output reg active
);
  localparam string ALIAS = LABEL;
  initial active = 1'b0;
  always @(posedge clk)
    active <= ALIAS == "enabled";
endmodule

module main(input clk);
  wire enabled, disabled;
  leaf #(.LABEL("enabled")) on(clk, enabled);
  leaf off(clk, disabled);

  disabled_stays_low: assert property (@(posedge clk) !disabled);
  enabled_rises: assert property (@(posedge clk) !enabled);
endmodule
