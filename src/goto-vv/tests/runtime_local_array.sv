module runtime_local_array(input logic clk);
  logic [1:0] phase = 0;
  always_ff @(posedge clk)
    if (phase < 2) phase <= phase + 1'b1;

  function automatic logic [11:0] combine(
    input logic use_max, input logic [11:0] a, b
  );
    return use_max ? (a > b ? a : b) : a + b;
  endfunction

  function automatic logic [11:0] reduce(
    input logic use_max, input logic [107:0] operands
  );
    logic [11:0] items[9];
    integer n;
    for (integer i = 0; i < 9; i++) items[i] = operands[i*12+:12];
    n = 9;
    for (integer layer = 0; layer < 4; layer++) begin
      for (integer i = 0; i < 4; i++)
        if (2*i + 1 < n)
          items[i] = combine(use_max, items[2*i], items[2*i+1]);
      if (n % 2) items[n/2] = items[n-1];
      n = (n + 1) / 2;
    end
    return items[0];
  endfunction

  function automatic logic signed [7:0] ascending(
    input logic signed [7:0] a, b
  );
    logic signed [7:0] items[-1:1];
    items[-1] = a;
    items[0] = b;
    items[1] = items[-1] + items[0];
    return items[1] >>> 1;
  endfunction

  function automatic logic signed [7:0] descending(
    input logic signed [7:0] a, b
  );
    logic signed [7:0] items[4:2];
    items[2] = a;
    items[4] = b;
    items[3] = items[2] + items[4];
    return items[3] >>> 1;
  endfunction

  wire [11:0] tail_word = phase == 0 ? 12'd40 : 12'd315;
  wire [107:0] sample = {tail_word, 12'd8, 12'd7, 12'd6, 12'd5,
                        12'd4, 12'd3, 12'd2, 12'd1};
  wire [11:0] sum = reduce(1'b0, sample);
  wire [11:0] maximum = reduce(1'b1, sample);
  wire signed [7:0] a = phase == 0 ? -8'sd7 : phase == 1 ? 8'sd120 : 8'sd127;
  wire signed [7:0] b = phase == 0 ? 8'sd2 : phase == 1 ? 8'sd12 : 8'sh80;
  wire signed [7:0] first = ascending(a, b);
  wire signed [7:0] second = ascending(b, b);
  wire signed [7:0] nested_call = ascending(ascending(a, b), a);
  wire signed [7:0] reversed = descending(a, b);

  odd_tail: assert property (@(posedge clk)
    sum == (phase == 0 ? 12'd76 : 12'd351) && maximum == tail_word);
  signed_endpoints: assert property (@(posedge clk)
    first == (phase == 0 ? -8'sd3 : phase == 1 ? -8'sd62 : -8'sd1) &&
    reversed == first);
  isolated_calls: assert property (@(posedge clk)
    second == (phase == 0 ? 8'sd2 : phase == 1 ? 8'sd12 : 8'sd0));
  nested: assert property (@(posedge clk)
    nested_call == (phase == 0 ? -8'sd5 : phase == 1 ? 8'sd29 : 8'sd63));
  wrong_tail: assert property (@(posedge clk) phase != 1 || sum == 12'd36);
endmodule
