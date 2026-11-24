module named_property_formal(input logic clk, data);
  property p(logic expected);
    @(posedge clk) data == expected;
  endproperty
  check_p: assert property (p(1'b1));
endmodule

module named_property_local(input logic clk, data);
  property p;
    bit held = 1'b1;
    @(posedge clk) data == held;
  endproperty
  check_p: assert property (p);
endmodule

module named_property_negedge(input logic clk, data);
  property p;
    @(negedge clk) data;
  endproperty
  check_p: assert property (p);
endmodule

module named_property_second_clock(input logic clk, other_clk, data);
  property p;
    @(posedge other_clk) data;
  endproperty
  primary: assert property (@(posedge clk) data);
  check_p: assert property (p);
endmodule

module named_property_body(input logic clk, data);
  property p;
    @(posedge clk) not data;
  endproperty
  check_p: assert property (p);
endmodule
