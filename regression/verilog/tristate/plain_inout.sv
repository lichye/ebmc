module terminal(inout [2:0] p, input d, output [2:0] seen);
  assign p[1] = d;
  assign seen = p;
endmodule

module main(input a, b);
  wire [2:0] line;
  wire [2:0] seen_a, seen_b;
  terminal left(line, a, seen_a);
  terminal right({line[2:1], line[0]}, b, seen_b);
  wire floating;
  assign line[0] = floating;
  aliases: assert property (seen_a == line && seen_b == line);
  agreed: assert property (a == b |-> line[1] == a);
  conflict: assert property (a != b |-> line[1] == a);
  source: assert property (line[0] == floating);
  free_top: assert property (line[2]);
endmodule
