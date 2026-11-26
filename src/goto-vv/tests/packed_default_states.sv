module packed_default_states(
  input logic clk, select,
  output logic [2:0] vec_x, vec_z, vec_select,
  output logic [3:0] fields
);
  typedef struct packed {
    logic head;
    logic [1:0] center;
    logic tail;
  } payload_t;
  payload_t payload;

`ifdef EXPLICIT_REFERENCE
  assign vec_x = {3{1'bx}};
  assign vec_z = {3{1'bz}};
  assign vec_select = {3{select ? 1'bx : 1'bz}};
  always @* begin
    payload.head = select;
    payload.center = 2'bxz;
    payload.tail = 1'bz;
  end
`else
  assign vec_x = '{default: 1'bx};
  assign vec_z = '{default: 1'bz};
  assign vec_select = '{default: (select ? 1'bx : 1'bz)};
  assign payload = '{head: select, center: 2'bxz, default: 1'bz};
`endif
  assign fields = payload;
  p_head: assert property (@(posedge clk) payload.head == select);
endmodule
