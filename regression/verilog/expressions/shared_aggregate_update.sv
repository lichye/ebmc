module main(input bit clk, select);
  typedef struct packed { bit [7:0] data; bit tag; } packet_t;
  packet_t cells[8];
  always_ff @(posedge clk)
    for(int i=0; i<8; i++)
      cells[i] <= select ? packet_t'(9'h0b5) : cells[0];
  p0: assert property (@(posedge clk) select |=> cells[7].data == 8'h5a && cells[7].tag);
  p1: assert property (@(posedge clk) ##1 (cells[0] == cells[7]));
endmodule
