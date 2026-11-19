module main(input bit clk, input bit d);
  wire en;
  wire [2:0] partial;
  assign partial[1] = d;
  wire [1:0] y = en ? 2'b01 : 2'b10;
  wire explicit_x = 1'bx;
  wire unused;
  wire [3:0] only_driven_read;
  assign only_driven_read[2] = d;
  tri1 pulled = en;
  same_frame: assert property (y != 0 && y == (en ? 2'b01 : 2'b10));
  stable_enable: assert property (##1 (en == $past(en)));
  stable_gap: assert property (##1 (partial[0] == $past(partial[0])));
  any_initial: assert property (en);
  driven: assert property (partial[1] == d && only_driven_read[2] == d && pulled);
  primary_can_change: assert property (##1 (d == $past(d)));
  literal_can_change: assert property (##1 (explicit_x == $past(explicit_x)));
  wire [1:0] bus;
  wire sense;
  reader u(bus, d, sense);
  port_alias: assert property (sense == bus[0]);
  port_stable: assert property (##1 (sense == $past(sense)));
endmodule

module reader(inout [1:0] pad, input bit d, output sense);
  assign pad[1] = d;
  assign sense = pad[0];
endmodule
