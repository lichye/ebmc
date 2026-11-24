module runtime_function(input logic clk);
  function automatic logic signed [6:0] add_ref(
      input logic signed [4:0] a, b);
    logic signed [6:0] value;
    value = 7'(a) + 7'(b);
    if (a < 0) return value - 7'sd1;
    return value;
  endfunction

  function automatic logic signed [6:0] op_ref(
      input int op, input logic signed [4:0] a, b);
    case (op)
      1: return add_ref(a, b);
      default: return 'x;
    endcase
  endfunction

  function automatic int count_down(input logic [4:0] a, input int width);
    int total;
    total = 0;
    for (int i = width - 1; i >= 0; i--)
      if (a[i]) total = total + (i + 1);
    return total;
  endfunction

  function automatic int adjust(input logic [4:0] a, input int width);
    return count_down(a, width) * 2;
  endfunction

  function automatic logic signed [4:0] narrow(
      input logic signed [4:0] a, b);
    return a + b;
  endfunction

  localparam logic signed [6:0] ONE = add_ref(5'sd1, 5'sd0);
  bit [2:0] frame = 0;
  always @(posedge clk)
    if (frame != 7) frame <= frame + 1'b1;

  wire logic signed [4:0] sampled =
    frame == 0 ? -5'sd3 : frame == 1 ? 5'sd5 : -5'sd7;
  wire logic signed [4:0] other =
    frame == 0 ? 5'sd5 : frame == 1 ? -5'sd4 : 5'sd2;
  wire logic signed [6:0] ab = op_ref(1, sampled, other);
  wire logic signed [6:0] ba = op_ref(1, other, sampled);
  wire logic signed [6:0] nested = op_ref(1, 5'(op_ref(1, sampled, other)), other);
  wire signed [31:0] loop3 = adjust(sampled, 3);
  wire signed [31:0] loop5 = adjust(sampled, 5);
  wire logic signed [4:0] boundary =
    narrow(frame[0] ? -5'sd16 : 5'sd15, frame[0] ? -5'sd1 : 5'sd1);

  const_ok: assert property (@(posedge clk) ONE == 7'sd1);
  default_history: assert property (@(posedge clk)
    (frame == 0) |-> (op_ref(1, $past(sampled, 2), $past(sampled)) == 7'sd0));
  partial_history: assert property (@(posedge clk)
    (frame == 1) |-> (op_ref(1, $past(sampled, 2), $past(sampled)) == -7'sd3));
  runtime_good: assert property (@(posedge clk) disable iff (frame == 0)
    (frame == 1) |=> (op_ref(1, $past(sampled, 2), $past(sampled)) == 7'sd1));
  runtime_bad: assert property (@(posedge clk) disable iff (frame == 0)
    (frame == 1) |=> (op_ref(1, $past(sampled, 2), $past(sampled)) == 7'sd2));
  next_good: assert property (@(posedge clk) disable iff (frame == 0)
    (frame == 2) |=> (op_ref(1, $past(sampled, 2), $past(sampled)) == -7'sd2));
  paired: assert property (@(posedge clk)
    ab == (frame < 2 ? 7'sd1 : -7'sd6) &&
    ba == (frame == 0 ? 7'sd2 : frame == 1 ? 7'sd0 : -7'sd5) &&
    nested == (frame == 0 ? 7'sd6 : frame == 1 ? -7'sd3 : -7'sd5));
  loops: assert property (@(posedge clk)
    loop3 == (frame < 2 ? 8 : 2) &&
    loop5 == (frame == 0 ? 26 : frame == 1 ? 8 : 20));
  signed_boundary: assert property (@(posedge clk)
    boundary == (frame[0] ? 5'sd15 : -5'sd16));
endmodule
