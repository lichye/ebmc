module counted_guard #(parameter int STEP = 2)(input logic clk);
  logic [1:0] phase = 0;
  wire [3:0][7:0] data =
    phase == 0 ? 32'hc974321b : phase == 1 ? 32'ha6f08523 : 32'h8146d9be;
  wire shift = phase != 1;
  logic [3:0][7:0] left, right;
  always_ff @(posedge clk)
    if (phase < 2)
      phase <= phase + 2'd1;

  always_comb begin
    if (shift) begin
      left = '0;
      right = '0;
      for (int i = 0; i < 4; i++)
        if (i >= STEP)
          left[i] = data[i - STEP];
      for (int i = 0; i < 4; i++) begin
        automatic int source_lane;
        source_lane = i + STEP;
        if (source_lane < 4)
          right[i] = data[source_lane];
      end
    end else begin
      left = data;
      right = data;
    end
  end

  p_first: assert property (@(posedge clk)
    phase == 0 |-> left == 32'h321b0000 && right == 32'h0000c974);
  p_pass: assert property (@(posedge clk)
    phase == 1 |-> left == 32'ha6f08523 && right == 32'ha6f08523);
  p_last: assert property (@(posedge clk)
    phase == 2 |-> left == 32'hd9be0000 && right == 32'h00008146);
  p_switch: assert property (@(posedge clk)
    phase == 0 |=> phase == 1 ##1 phase == 2);
  p_wrong: assert property (@(posedge clk)
    phase == 2 |-> left != 32'hd9be0000);
endmodule
