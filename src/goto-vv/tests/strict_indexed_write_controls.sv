module strict_indexed_write_controls(
  input logic clk, input logic [2:0] index, data,
  output logic [7:0] q
);
  always_ff @(posedge clk)
`ifdef EBMC_STATIC_OOB
    q[7 +: 3] <= data;
`else
    q[index +: 3] <= data;
`endif
endmodule
