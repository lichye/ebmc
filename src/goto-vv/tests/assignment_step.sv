module assignment_step(input logic clk);
  localparam int ONE = 1;
  logic [1:0] phase = 0;
  wire write_enable = phase < 2;
  wire addr = phase[0];
  wire [3:0] mask = phase == 0 ? 4'b0101 : 4'b1111;
  wire [15:0] payload = phase == 0 ? 16'h5678 : phase == 1 ? 16'ha9bc : 16'hdead;
  logic [15:0] mem [0:1];
  wire [15:0] bank0 = mem[0], bank1 = mem[1];

  initial begin
    mem[0] = 16'h1234;
    mem[1] = 16'h2679;
  end
  always @(posedge clk) begin
    if (phase < 3)
      phase <= phase + 2'd1;
    if (write_enable)
      for (int i = 0; i < 4; i = i + 1)
        if (mask[i])
          mem[addr][4*i +: 4] <= payload[4*i +: 4];
  end

  wire [3:0][7:0] lanes = 32'h81236741;
  logic [3:0][7:0] wrapped;
  logic [2:0] signed_seen;
  logic single_seen;
  always_comb begin
    wrapped = '0;
    signed_seen = '0;
    single_seen = 0;
    for (logic [1:0] i = 3; i != 1; i = i + ONE)
      wrapped[i] = lanes[i];
    for (logic signed [2:0] i = 3; i != -3; i = i + 1)
      if (i == 3)
        signed_seen[0] = 1;
      else if (i == -4)
        signed_seen[1] = 1;
      else
        signed_seen[2] = 1;
    for (bit i = 1; i != 0; i = i + 1)
      single_seen = i;
  end

  p_initial: assert property (@(posedge clk)
    phase == 0 |-> {bank0, bank1} == 32'h12342679);
  p_partial: assert property (@(posedge clk)
    phase == 1 |-> {bank0, bank1} == 32'h16382679);
  p_full: assert property (@(posedge clk)
    phase == 2 |-> {bank0, bank1} == 32'h1638a9bc);
  p_hold: assert property (@(posedge clk)
    phase == 3 |-> {bank0, bank1} == 32'h1638a9bc);
  p_progress: assert property (@(posedge clk)
    phase == 0 |=> phase == 1 ##1 phase == 2 ##1 phase == 3);
  p_widths: assert property (@(posedge clk)
    wrapped == 32'h81000041 && signed_seen == 3'b011 && single_seen);
  p_wrong: assert property (@(posedge clk)
    phase == 3 |-> bank1 != 16'ha9bc);
endmodule
