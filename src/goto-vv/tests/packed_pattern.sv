module packed_pattern(input logic clk, input logic rst, input logic [2:0] tail_input);
  typedef struct packed {
    logic [2:0] tag;
    logic signed [3:0] balance;
  } inner_t;

  typedef struct packed {
    logic [1:0] hi;
    inner_t nested;
    logic [2:0] tail;
  } state_t;

  typedef struct packed { logic flag; } single_t;
  single_t single_value;
  assign single_value = '{default: 1'b1};

  state_t self, pattern_value, overridden, four_state;
  assign pattern_value = '{tail: tail_input, default: 2, hi: 2'd1};
  assign overridden = '{tail: 3'd5, nested: '{default: 3, balance: -5'sd2}, default: 1};
  assign four_state = '{tail: 3'b1xz, default: 0};

  always_ff @(posedge clk) begin
    if (rst)
      self <= '{default: 0};
    else
      self <= pattern_value;
  end

  p_reset: assert property (@(posedge clk) rst |=> self == 12'h000);
  p_layout: assert property (@(posedge clk)
    pattern_value == {9'b010100010, tail_input});
  p_override: assert property (@(posedge clk)
    overridden == 12'h5f5 && overridden.nested.balance == -4'sd2);
  p_single: assert property (@(posedge clk) single_value.flag);
  p_known: assert property (@(posedge clk) four_state.tail[2] == 1'b1);
  p_wrong: assert property (@(posedge clk)
    tail_input == 3'd5 |-> pattern_value != 12'h515);
endmodule
