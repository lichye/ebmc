module automatic_locals(input logic clk);
  localparam int LANES = 2;
  logic phase = 1'b0;
  wire [3:0][7:0] in_scale = phase ? 32'h04080602 : 32'h07010309;
  wire [3:0][7:0] payload = 32'h80304060;
  logic [3:0][7:0] max_scale, shifted;

  always_ff @(posedge clk)
    phase <= 1'b1;

  always_comb begin
    for (int blk = 0; blk < 2; blk++) begin
      automatic logic [7:0] blk_max;
      blk_max = in_scale[blk*LANES];
      for (int e = 1; e < LANES; e++) begin
        if (in_scale[blk*LANES+e] > blk_max)
          blk_max = in_scale[blk*LANES+e];
      end
      for (int e = 0; e < LANES; e++) begin
        automatic int k;
        automatic logic [7:0] sh;
        k = blk*LANES + e;
        sh = blk_max - in_scale[k];
        shifted[k] = $signed(payload[k]) >>> sh;
        max_scale[k] = blk_max;
      end
    end
  end

  p_first: assert property (@(posedge clk)
    !phase |-> max_scale == 32'h07070909 && shifted == 32'h80000160);
  p_second: assert property (@(posedge clk)
    phase |-> max_scale == 32'h08080606 && shifted == 32'hF8304006);
  p_switch: assert property (@(posedge clk) !phase |=> phase);
  p_wrong: assert property (@(posedge clk) !phase || max_scale != 32'h08080606);
endmodule
