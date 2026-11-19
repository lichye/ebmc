module leaf(inout [1:0] io, input [1:0] d, input en, output [1:0] seen);
  assign io = en ? d : 2'bzz;
  assign seen = io;
endmodule

module middle(inout [1:0] io, input [1:0] d, input en, output [1:0] seen);
  leaf inner(io, d, en, seen);
endmodule

module open_port(inout io, output tri1 pulled);
  assign pulled = io;
endmodule

module main(input [1:0] a, b, input e1, e2);
  wire open_value;
  open_port unused(.pulled(open_value));
  released_port: assert property (open_value);
  tri1 [3:0] line;
  wire [1:0] first, second;
  middle m({line[0], line[2]}, a, e1, first);
  leaf n({line[0], line[2]}, b, e2, second);
  same_sample: assert property (first == second && first == {line[0],line[2]});
  from_child: assert property ((e1 && !e2) |-> first == a);
  from_sibling: assert property ((!e1 && e2) |-> first == b);
  pull: assert property ((!e1 && !e2) |-> line == 4'hf);
  conflict: assert property ((e1 && e2 && a != b) |-> first == a);
endmodule
