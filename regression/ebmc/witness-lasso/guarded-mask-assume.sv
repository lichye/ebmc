module main(input bit clk, wr, input bit [1:0] addr, input bit [7:0] data);
  bit [7:0] mem [0:3];
  bit valid = 0;
  bit [1:0] addr_q;
  always @(posedge clk) begin
    valid <= wr;
    if (wr) begin
      mem[addr] <= data;
      addr_q <= addr;
    end
  end
  only_a5: assume property (!wr || data == 8'ha5);
  good: assert property (!valid || mem[addr_q] == 8'ha5);
  bad: assert property (!valid || mem[addr_q] == 8'h5a);
endmodule
