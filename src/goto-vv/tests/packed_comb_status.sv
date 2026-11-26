module packed_comb_status(input logic clk);
  localparam int NUM_LANES = 3;
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;

  bit [1:0] phase = 0;
  always @(posedge clk) phase <= phase + 1'b1;

  wire [NUM_LANES-1:0][4:0] lane_status = {5'b01001, 5'b00110, 5'b10000};
  wire [NUM_LANES-1:0] lane_masks =
    phase == 0 ? 3'b111 : phase == 1 ? 3'b101 : phase == 2 ? 3'b000 : 3'b010;
  logic [NUM_LANES-1:0][4:0] partial_status;
  status_t status, independent;
  logic saw_nv;

  always_comb begin : output_processing
`ifdef EXPLICIT_REFERENCE
    logic [4:0] temp_status;
    temp_status = '0;
    temp_status = temp_status | (lane_status[0] & {5{lane_masks[0]}});
    partial_status[0] = temp_status;
    temp_status = temp_status | (lane_status[1] & {5{lane_masks[1]}});
    partial_status[1] = temp_status;
    temp_status = temp_status | (lane_status[2] & {5{lane_masks[2]}});
    partial_status[2] = temp_status;
    saw_nv = temp_status[4];
`else
    automatic status_t temp_status;
    temp_status = '0;
    for (int i = 0; i < int'(NUM_LANES); i++) begin
      temp_status |= lane_status[i] & {5{lane_masks[i]}};
      partial_status[i] = temp_status;
    end
    saw_nv = 0;
    if (temp_status.nv)
      saw_nv = 1;
`endif
    status = temp_status;
  end

  always_comb begin : other_status
`ifdef EXPLICIT_REFERENCE
    logic [4:0] temp_status;
    temp_status = '0;
    temp_status = temp_status | (lane_status[1] & {5{lane_masks[1]}});
`else
    automatic status_t temp_status;
    temp_status = '0;
    temp_status |= lane_status[1] & {5{lane_masks[1]}};
`endif
    independent = temp_status;
  end

  p_full: assert property (@(posedge clk) phase == 0 |-> status == 5'b11111);
  p_sparse: assert property (@(posedge clk) phase == 1 |-> status == 5'b11001);
  p_clear: assert property (@(posedge clk) phase == 1 |=> status == 5'b00000);
  p_middle: assert property (@(posedge clk) phase == 3 |-> status == 5'b00110);
  p_steps: assert property (@(posedge clk)
    partial_status[0] == (lane_status[0] & {5{lane_masks[0]}}) &&
    partial_status[1] == (partial_status[0] | (lane_status[1] & {5{lane_masks[1]}})) &&
    partial_status[2] == (partial_status[1] | (lane_status[2] & {5{lane_masks[2]}})));
  p_members: assert property (@(posedge clk) phase == 1 |->
    status.nv && status.dz && !status.of && !status.uf && status.nx);
  p_current_branch: assert property (@(posedge clk) saw_nv == status.nv);
  p_independent: assert property (@(posedge clk)
    independent == (lane_status[1] & {5{lane_masks[1]}}));
  p_wrong: assert property (@(posedge clk) phase == 1 |-> status != 5'b11001);
endmodule
