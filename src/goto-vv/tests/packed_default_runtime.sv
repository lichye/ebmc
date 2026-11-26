module packed_default_runtime(input logic clk);
  bit [1:0] tick = 0;
  always @(posedge clk) tick <= tick + 1'b1;

  wire fill = tick[0];
  wire [7:0] op_result = 8'ha5;
  wire signed [2:0] data = fill ? -3'sd3 : 3'sd2;
  typedef struct packed {
    logic narrow;
    logic signed [1:0] low;
    logic signed [4:0] wide;
    logic [2:0] fixed_tag;
  } payload_t;

  wire [6:2] descending;
  wire [3:6] ascending;
  wire signed [7:7] single;
  wire [7:0] conditional_value;
  wire [1:0] constant_value;
  payload_t payload, reference_payload;

`ifdef EXPLICIT_REFERENCE
  assign descending = {5{fill}};
  assign ascending = {4{data[0]}};
  assign single = fill;
  assign conditional_value = tick[1] ? op_result : {8{fill}};
  assign constant_value = 2'b11;
  always @* begin
    payload.narrow = data[0];
    payload.low = data;
    payload.wide = data;
    payload.fixed_tag = 3'b101;
  end
`else
  assign descending = '{default: fill};
  assign ascending = '{default: data};
  assign single = '{default: fill};
  assign conditional_value = tick[1] ? op_result : '{default: fill};
  assign constant_value = '{default: 1'b1};
  assign payload = '{fixed_tag: 3'b101, default: data};
`endif

  always @* begin
    reference_payload.narrow = data[0];
    reference_payload.low = data;
    reference_payload.wide = data;
    reference_payload.fixed_tag = 3'b101;
  end
  p_vectors: assert property (@(posedge clk)
    descending == {5{fill}} && ascending == {4{fill}} &&
    single == fill && constant_value == 2'b11);
  p_branches: assert property (@(posedge clk)
    conditional_value == (tick[1] ? op_result : {8{fill}}));
  p_payload: assert property (@(posedge clk) payload == reference_payload);
  p_order: assert property (@(posedge clk)
    payload == (fill ? 11'b1_01_11101_101 : 11'b0_10_00010_101));
  p_wrong_vector: assert property (@(posedge clk)
    tick == 1 |-> descending != 5'b11111);
  p_wrong_payload: assert property (@(posedge clk)
    tick == 1 |-> payload.wide != -5'sd3);
endmodule
