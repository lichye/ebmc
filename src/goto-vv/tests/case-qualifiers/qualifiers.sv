module case_guarded(input [1:0] sel, input enable, output reg result);
  always_comb begin
    result = 1'b0;
    if (enable) begin
      priority case (sel)  // nested_missing
        2'd0: result = 1'b1;
      endcase
    end
  end
endmodule

module qualifiers(input clk_i);
  bit [2:0] tick = 3'd0;
  always @(posedge clk_i)
    if (tick != 3'd7) tick <= tick + 3'd1;

  wire [1:0] sel = tick[1:0];
  bit [1:0] sampled_sel, label_at_entry;
  bit [2:0] u_default, u_missing, p_default, p_missing;
  bit safe;
  wire nested;
  case_guarded child(.sel(sel), .enable(tick == 3'd5), .result(nested));

  always_comb begin
    sampled_sel = sel;
    label_at_entry = 2'd1;
    u_default = 3'd7;
    unique case (sampled_sel)  // overlap_with_default
      2'd0, 2'd0, 2'd1: begin
        u_default = 3'd1;
        sampled_sel = 2'd3;
        label_at_entry = 2'd0;
      end
      label_at_entry: u_default = 3'd2;
      2'd2: ;
      default: ;
    endcase

    u_missing = 3'd7;
    unique case (sel)  // unique_without_default
      2'd0, 2'd0, 2'd1: u_missing = 3'd1;
      2'd1: u_missing = 3'd2;
    endcase

    priority case (sel)  // priority_with_default
      2'd0, 2'd1: p_default = 3'd1;
      default: p_default = 3'd4;
      2'd1: p_default = 3'd2;
    endcase

    p_missing = 3'd7;
    priority case (sel)  // priority_without_default
      2'd0, 2'd1: p_missing = 3'd1;
      2'd1: p_missing = 3'd2;
    endcase

    safe = 1'b0;
    if (tick < 3'd2)
      if (sel == 2'd0) begin
        priority case (sel)  // guarded_safe
          2'd0: safe = 1'b1;
        endcase
      end
  end

  p_values: assert property (@(posedge clk_i)
    ((sel < 2'd2) ? {u_default, u_missing, p_default, p_missing} == {4{3'd1}}
                  : {u_default, u_missing, p_default, p_missing} == {3'd7, 3'd7, 3'd4, 3'd7}) &&
    safe == (tick == 3'd0) && !nested);
  c_overlap: cover property (@(posedge clk_i) tick == 3'd1 && u_default == 3'd1 && p_missing == 3'd1);
  c_hole: cover property (@(posedge clk_i) tick == 3'd2 && u_missing == 3'd7 && p_missing == 3'd7);
  c_nested: cover property (@(posedge clk_i) tick == 3'd5 && !nested);
endmodule
