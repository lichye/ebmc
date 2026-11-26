module clocked_loop_local(input clk_i, input [2:0] variable_count);
  bit [31:0] mem [0:1];
  bit [31:0] old_word;
  bit [31:0] ref0 = 0, ref1 = 0, ref_old;
  bit signed [7:0] first_index, last_index;
  bit [2:0] tick = 0;
  wire addr = tick == 3'd1 || tick == 3'd3 || tick == 3'd5;
  wire enable = tick != 3'd4 && tick != 3'd6;
  wire write_word = tick != 3'd3;
  wire [3:0] mask = tick == 3'd2 ? 4'b0101 : tick == 3'd5 ? 4'b1000 : 4'b1111;
  wire [31:0] wdata = tick == 3'd0 ? 32'h11223344 : tick == 3'd1 ? 32'haabbccdd :
                      tick == 3'd2 ? 32'hffeeddcc : 32'h55667788;
  wire [31:0] ref_selected = addr ? ref1 : ref0;
  wire [31:0] new_ref = {
    mask[3] ? wdata[31:24] : ref_selected[31:24],
    mask[2] ? wdata[23:16] : ref_selected[23:16],
    mask[1] ? wdata[15:8] : ref_selected[15:8],
    mask[0] ? wdata[7:0] : ref_selected[7:0]
  };

  initial begin
    mem[0] = 0;
    mem[1] = 0;
  end
  always @(posedge clk_i) begin
    if (tick < 3'd6) tick <= tick + 3'd1;
    begin : mem_write
      integer i;
      for (i = 0; i < 4; i = i + 1) begin
        if (enable & write_word & mask[i]) mem[addr][i*8+:8] <= wdata[8*i+:8];
      end
    end
    begin : signed_index
      integer i;
      for (i = -1; i < 2; i = i + 1) begin
        if (i == -1) first_index <= i;
        if (i == 1) last_index <= i;
      end
    end
    old_word <= mem[addr];
    ref_old <= ref_selected;
    if (enable && write_word) begin
      if (addr) ref1 <= new_ref;
      else ref0 <= new_ref;
    end
  end

  p_reference: assert property (@(posedge clk_i)
    mem[0] == ref0 && mem[1] == ref1 && (tick == 0 || old_word == ref_old));
  p_endpoints: assert property (@(posedge clk_i)
    tick != 0 |-> first_index == -8'sd1 && last_index == 8'sd1);
  p_full: assert property (@(posedge clk_i)
    tick == 2 |-> mem[0] == 32'h11223344 && mem[1] == 32'haabbccdd && old_word == 0);
  p_sparse: assert property (@(posedge clk_i)
    tick == 3 |-> mem[0] == 32'h11ee33cc && mem[1] == 32'haabbccdd && old_word == 32'h11223344);
  p_disabled: assert property (@(posedge clk_i)
    (tick == 4 || tick == 5) |-> mem[0] == 32'h11ee33cc && mem[1] == 32'haabbccdd);
  p_address: assert property (@(posedge clk_i)
    tick == 6 |-> mem[0] == 32'h11ee33cc && mem[1] == 32'h55bbccdd);
  p_wrong_old: assert property (@(posedge clk_i) tick == 3 |-> old_word == 32'h11ee33cc);
endmodule
