module bad_array_dynamic(input logic clk, d);
  function automatic logic f(input logic value);
    logic items[];
    return value;
  endfunction
  p: assert property (@(posedge clk) f(d));
endmodule

module bad_array_static(input logic clk, d);
  function automatic logic f(input logic value);
    static logic items[2];
    items[0] = value;
    return items[0];
  endfunction
  p: assert property (@(posedge clk) f(d));
endmodule

module bad_array_nested(input logic clk, d);
  function automatic logic f(input logic value);
    logic items[2][3];
    items[0][0] = value;
    return items[0][0];
  endfunction
  p: assert property (@(posedge clk) f(d));
endmodule

module bad_array_nonlocal(input logic clk, d);
  logic items[2];
  function automatic logic f(input logic value);
    items[0] = value;
    return items[0];
  endfunction
  p: assert property (@(posedge clk) f(d));
endmodule
