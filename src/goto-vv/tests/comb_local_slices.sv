module special_value #(parameter W = 5) (
  input logic negative, top_bit,
  output wire signed [11:0] result
);
  logic signed [W-1:0] value;
`ifdef EXPLICIT_REFERENCE
  assign value = negative ? ~{top_bit, {(W-1){1'b1}}}
                          :  {top_bit, {(W-1){1'b1}}};
`else
  always_comb begin
    automatic logic signed [W-1:0] bits;
    bits[W-2:0] = '1;
    bits[W-1] = top_bit;
    if (negative)
      bits = ~bits;
    value = bits;
  end
`endif
  assign result = value;
endmodule

module comb_local_slices(input logic clk, input logic [7:0] data);
  bit [2:0] phase = 0;
  always @(posedge clk) phase <= phase + 1'b1;

  wire signed [11:0] narrow, wide;
  special_value #(5) u5(phase[0], phase[1], narrow);
  special_value #(8) u8(phase[0], phase[1], wide);
  logic [7:0] forward, reverse;
  logic [3:0] joined;
  logic [2:0] transient_bits;
  logic [1:0] from_whole_or_slice;
  logic sole_bit;

`ifdef EXPLICIT_REFERENCE
  assign forward = {data[7:6], data[0], data[2], phase[0], data[2:0]};
  assign reverse = forward;
  assign joined = {phase[1] ? data[3:2] : data[7:6], data[1:0]};
  assign transient_bits = {data[0] ? data[3] : data[7], data[1:0]};
  assign from_whole_or_slice = phase[0] ? data[1:0] : data[5:4];
  assign sole_bit = data[4];
`else
  always_comb begin
    automatic logic [2:9] ascending;
    automatic logic [9:2] descending;
    automatic logic [5:0] joined_paths;
    automatic logic [7:0] partial;
    automatic logic [3:0] mixed;
    automatic logic [4:4] single;

    ascending[2:5] = data[7:4];
    ascending[6 +: 4] = data[3:0];
    ascending[4:6] = {data[0], ascending[7], phase[0]};

    descending[9:6] = data[7:4];
    descending[5 -: 4] = data[3:0];
    descending[7:5] = {data[0], descending[4], phase[0]};

    joined_paths[1:0] = data[1:0];
    if (phase[1])
      joined_paths[5:2] = data[5:2];
    else
      joined_paths[3:2] = data[7:6];
    joined = joined_paths[3:0];

    partial[1:0] = data[1:0];
    if (partial[0])
      partial[2] = data[3];
    else
      partial[2] = data[7];
    transient_bits = partial[2:0];

    if (phase[0])
      mixed = data[3:0];
    else
      mixed[1:0] = data[5:4];
    from_whole_or_slice = mixed[1:0];

    single[4] = data[4];
    sole_bit = single[4];
    forward = ascending;
    reverse = descending;
  end
`endif

  p_sign: assert property (@(posedge clk)
    narrow == (phase[0] ? (phase[1] ? 12'h000 : 12'hff0)
                        : (phase[1] ? 12'hfff : 12'h00f)) &&
    wide == (phase[0] ? (phase[1] ? 12'h000 : 12'hf80)
                      : (phase[1] ? 12'hfff : 12'h07f)));
  p_order: assert property (@(posedge clk)
    forward == {data[7:6], data[0], data[2], phase[0], data[2:0]} &&
    reverse == forward && sole_bit == data[4]);
  p_paths: assert property (@(posedge clk)
    joined == {phase[1] ? data[3:2] : data[7:6], data[1:0]} &&
    transient_bits == {data[0] ? data[3] : data[7], data[1:0]} &&
    from_whole_or_slice == (phase[0] ? data[1:0] : data[5:4]));
  p_wrong: assert property (@(posedge clk) phase != 3'd2 || narrow == 12'hffe);
endmodule
