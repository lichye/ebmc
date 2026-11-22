module main(input bit clk, rst, a, b, c);
  empty_antecedent: assert property (@(posedge clk) disable iff (rst)
    a[*0:$] |=> c);
  plus_based: assert property (@(posedge clk) disable iff (rst)
    (a ##1 a[+] ##1 b) |=> c);
endmodule
