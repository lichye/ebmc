module main(input bit clk, wr, input bit [1:0] addr, input bit [7:0] data);
  bit [7:0] mem [0:3];
  bit valid = 0;
  bit [1:0] addr_q;
  bit [7:0] data_q;
  always @(posedge clk) begin
    valid <= wr;
    if (wr) begin
      mem[addr] <= data;
      addr_q <= addr;
      data_q <= data;
    end
  end
  readback: assert property (!valid || mem[addr_q] == data_q);
  wrong: assert property (!valid || mem[addr_q] != data_q);
endmodule
