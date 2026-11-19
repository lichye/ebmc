module main(input bit clk, input bit [3:0] a);
  property increment;
    bit [3:0] saved;
    @(posedge clk) (1, saved = a) ##1 first_match(1, saved++)
      |-> saved == ($past(a) + 4'd1);
  endproperty
  property stale;
    bit [3:0] saved;
    @(posedge clk) (1, saved = a) ##1 first_match(1, saved++)
      |-> saved == $past(a);
  endproperty
  property ordered;
    bit [3:0] saved;
    @(posedge clk) (1, saved = a) ##1 (1, saved += 4'd2, --saved)
      |-> saved == ($past(a) + 4'd1);
  endproperty
  p0: assert property (increment);
  p1: assert property (stale);
  p2: assert property (ordered);
endmodule
