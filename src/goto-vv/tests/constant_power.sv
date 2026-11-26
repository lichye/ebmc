// Model the cc_lzc power-shaped selectors with both a scalar and a packed tree.
module power_tree #(
  parameter int Width = 5,
  parameter bit Wrong = 0,
  localparam int Levels = $clog2(Width),
  localparam int Nodes = 2 ** Levels - 1
)(
  input logic [Width-1:0] data,
  output wire [Nodes-1:0] sel,
  output wire [Nodes-1:0][Levels-1:0] index
);
  for (genvar level = 0; level < Levels; level++) begin : levels
    if (level == Levels - 1) begin : leaf
      for (genvar k = 0; k < 2 ** level; k++) begin : group
        if (k * 2 + 1 < Width) begin : pair
          assign sel[2 ** level - 1 + k] = data[k * 2] | data[k * 2 + 1];
          assign index[2 ** level - 1 + k] =
              data[k * 2] ? Levels'(k * 2) : Levels'(k * 2 + 1);
        end else if (k * 2 < Width) begin : single
          assign sel[2 ** level - 1 + k] = data[k * 2];
          assign index[2 ** level - 1 + k] = Levels'(k * 2);
        end else begin : pad
          assign sel[2 ** level - 1 + k] = 1'b0;
          assign index[2 ** level - 1 + k] = Levels'(Width - 1);
        end
      end
    end else begin : branch
      for (genvar l = 0; l < 2 ** level; l++) begin : group
        assign sel[2 ** level - 1 + l] =
            sel[2 ** (level + 1) - 1 + l * 2] | sel[2 ** (level + 1) - 1 + l * 2 + 1];
        assign index[2 ** level - 1 + l] = sel[2 ** (level + 1) - 1 + l * 2] ?
            index[2 ** (level + 1) - 1 + l * 2 + (Wrong && level == 0 ? 1 : 0)] :
            index[2 ** (level + 1) - 1 + l * 2 + 1];
      end
    end
  end
endmodule

// Each reference group scans its input interval directly. The index of
// an empty group is the last element in the interval, clipped to Width.
module power_tree_reference #(
  parameter int Width = 5,
  localparam int Levels = $clog2(Width),
  localparam int Leaves = 1 << Levels,
  localparam int Nodes = Leaves - 1
)(
  input logic [Width-1:0] data,
  output wire [Nodes-1:0] sel,
  output wire [Nodes-1:0][Levels-1:0] index
);
  wire [Leaves-1:0] padded = {{(Leaves-Width){1'b0}}, data};
  for (genvar level = 0; level < Levels; level++) begin : levels
    localparam int GroupSize = 1 << (Levels - level);
    for (genvar k = 0; k < (1 << level); k++) begin : group
      localparam int Lo = k * GroupSize;
      localparam int Hi = Lo + GroupSize - 1;
      localparam int Node = (1 << level) - 1 + k;
      wire [GroupSize:0][Levels-1:0] scan;
      assign scan[GroupSize] = Levels'(Hi < Width ? Hi : Width - 1);
      for (genvar g = 0; g < GroupSize; g++) begin : slot
        assign scan[g] = padded[Lo + g] ? Levels'(Lo + g) : scan[g+1];
      end
      assign sel[Node] = |padded[Lo +: GroupSize];
      assign index[Node] = scan[0];
    end
  end
endmodule

module constant_power(input bit clk, input logic [4:0] a, input logic [8:0] b);
  wire [6:0] sel_a, ref_sel_a, bad_sel;
  wire [6:0][2:0] index_a, ref_index_a, bad_index;
  wire [14:0] sel_b, ref_sel_b;
  wire [14:0][3:0] index_b, ref_index_b;
  power_tree dut_a(a, sel_a, index_a);
  power_tree_reference ref_a(a, ref_sel_a, ref_index_a);
  power_tree #(.Width(9)) dut_b(b, sel_b, index_b);
  power_tree_reference #(.Width(9)) ref_b(b, ref_sel_b, ref_index_b);
  power_tree #(.Wrong(1)) corrupt(a, bad_sel, bad_index);

  p_default: assert property (@(posedge clk) sel_a == ref_sel_a && index_a == ref_index_a);
  p_override: assert property (@(posedge clk) sel_b == ref_sel_b && index_b == ref_index_b);
  p_wrong_child: assert property (@(posedge clk) bad_sel == ref_sel_a && bad_index == ref_index_a);

  wire signed [7:0] odd = (-8'sd3) ** 6'd3;
  wire signed [7:0] even = (-8'sd3) ** 6'd4;
  wire signed [3:0] signed_wrap = 4'sd3 ** 6'd2;
  wire [3:0] truncated = 4'd3 ** 4;
  wire [11:0] context_wide = 4'd3 ** 4;
  wire [11:0] explicitly_narrow = 4'(4'd3 ** 4);
  wire [39:0] wide_result = 3'd2 ** 35;
  wire [3:0] self_determined_exponent = 4'd2 ** 6'd16;
  wire [7:0] nested = (2 ** (1+2)) ** 2;
  wire [7:0] negative_exponent = 8'd2 ** (-4'sd1);

  p_sign: assert property (@(posedge clk) odd == -8'sd27 && odd < 0 &&
      even == 8'sd81 && signed_wrap == -4'sd7 && signed_wrap < 0);
  p_width: assert property (@(posedge clk) truncated == 4'd1 && context_wide == 12'd81 &&
      explicitly_narrow == 12'd1 && wide_result == 40'h0800000000);
  p_exponent: assert property (@(posedge clk) self_determined_exponent == 0 &&
      nested == 8'd64 && negative_exponent == 0);
endmodule

module power_runtime_exponent(input bit clk, input logic [7:0] n);
  wire [7:0] y = 8'd3 ** n;
  p: assert property (@(posedge clk) y == 0);
endmodule

module power_runtime_base(input bit clk, input logic [7:0] n);
  wire [7:0] y = n ** 8'd2;
  p: assert property (@(posedge clk) y == 0);
endmodule

module power_procedural_local(input bit clk, output logic [7:0] y);
  always_comb begin
    logic [7:0] value;
    value = 8'd3;
    y = value ** 8'd2;
  end
  p: assert property (@(posedge clk) y == 9);
endmodule
