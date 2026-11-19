module main(input [1:0] d, input en);
  trireg [1:0] cells [0:1];
  assign cells[1] = en ? d : 2'bzz;
  transparent: assert property (en |-> cells[1] == d);
  held: assert property (1'b1 |=> (en || $stable(cells[1])));
  untouched: assert property (1'b1 |=> $stable(cells[0]));

  wire [1:0] lanes [0:1];
  assign lanes[0] = en ? d : 2'bzz;
  tri1 [1:0] pulled = lanes[0];
  released: assert property (!en |-> pulled == 2'b11);
  driven: assert property (en |-> pulled == d);
endmodule
