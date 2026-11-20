module leaf #(parameter string LABEL = "default") (output wire hit, earlier);
  localparam string ALIAS = LABEL;
  assign hit = "xyz" == ALIAS;
  assign earlier = ALIAS < "t";
endmodule

module main;
  localparam string TARGET = "xyz";
  wire a, b, c, ax, bx, cx;
  leaf #(.LABEL(TARGET)) la(a, ax);
  leaf #("") lb(b, bx);
  leaf lc(c, cx);
  selected: assert final (a && !b && !c && !ax && bx && cx);
  reachable: assert final (!a);
endmodule
