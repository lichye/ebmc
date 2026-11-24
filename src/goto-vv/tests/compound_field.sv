module compound_field(
  input logic clk,
  input logic [2:0] write_seed,
  input logic signed [2:0] signed_seed,
  input logic [3:0] neighbor_seed
);
  typedef struct packed {
    logic [2:0] write_ptr;
    logic signed [2:0] balance;
    logic [3:0] neighbor;
  } fifo_state_t;

  fifo_state_t self, next_self;
  logic [2:0] after_add;
  logic signed [2:0] after_sub;
  wire [2:0] write_result = next_self.write_ptr;
  wire signed [2:0] signed_result = next_self.balance;
  wire [3:0] neighbor_result = next_self.neighbor;

  assign self = {write_seed, signed_seed, neighbor_seed};
  always_comb begin
    next_self = self;
    next_self.write_ptr += 5'd2;
    after_add = next_self.write_ptr;
    next_self.balance -= 5'sd1;
    after_sub = next_self.balance;
    next_self.write_ptr -= 5'd1;
    next_self.balance += -5'sd2;
  end

  p_wrap: assert property (@(posedge clk)
    write_seed == 3'd7 |-> after_add == 3'd1 && write_result == 3'd0);
  p_signed: assert property (@(posedge clk)
    signed_seed == 3'sb100 |-> after_sub == 3'sd3 && signed_result == 3'sd1);
  p_adjacent: assert property (@(posedge clk) neighbor_result == neighbor_seed);
  p_wrong: assert property (@(posedge clk)
    (write_seed == 3'd7 && signed_seed == 3'sb100 && neighbor_seed == 4'ha)
    |-> write_result != 3'd0);
endmodule
