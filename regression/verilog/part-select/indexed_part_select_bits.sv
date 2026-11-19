module main(input logic [11:0] bus, input bit [11:0] bits_bus,
            input bit [4:0] base, output wire [2:0] high);
  assign high = bus[$bits(bus)-1 -: 3];

  static_slice: assert final (high == bus[11:9]);
  dynamic_slice: assert property (base == 5 |-> bus[base -: 3] === bus[5:3]);
  mixed_base: assert property (base == 2 |-> bus[$bits(bus)-1-base -: 3] === bus[9:7]);
  partial_logic: assert final (bus[$bits(bus) -: 3] === {1'bx, bus[11:10]});
  partial_bit: assert final (bits_bus[$bits(bits_bus) -: 3] == {1'b0, bits_bus[11:10]});
  outside_logic: assert final (bus[$bits(bus)+2 -: 3] === 3'bxxx);
  outside_bit: assert final (bits_bus[$bits(bits_bus)+2 -: 3] == 3'b000);
endmodule
