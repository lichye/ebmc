module runtime_function_recursive(input logic clk, input int a);
  function automatic int f(input int value);
    if (value <= 0) return 0;
    return f(value - 1);
  endfunction
  p: assert property (@(posedge clk) f(a) == 0);
endmodule

module runtime_function_out(input logic clk, input int a);
  int written;
  function automatic int f(input int value, output int dst);
    dst = value + 1;
    return dst;
  endfunction
  int result;
  always_comb result = f(a, written);
  p: assert property (@(posedge clk) result == 0);
endmodule

module runtime_function_ref(input logic clk, input int a);
  int written;
  function automatic int f(input int value, ref int dst);
    dst = value + 1;
    return dst;
  endfunction
  int result;
  always_comb result = f(a, written);
  p: assert property (@(posedge clk) result == 0);
endmodule

module runtime_function_nonlocal(input logic clk, input int a);
  int observed;
  function automatic int f(input int value);
    observed = value;
    return value;
  endfunction
  int result;
  always_comb result = f(a);
  p: assert property (@(posedge clk) result == 0);
endmodule

module runtime_function_unsupported(input logic clk, input int a);
  function automatic int f(input int value);
    repeat (2) value = value + 1;
    return value;
  endfunction
  p: assert property (@(posedge clk) f(a) == 0);
endmodule

module runtime_function_static(input logic clk, input int a);
  function static int f(input int value);
    return value;
  endfunction
  p: assert property (@(posedge clk) f(a) == 0);
endmodule

module runtime_function_dynamic_loop(input logic clk, input int a);
  function automatic int f(input int value);
    int result;
    result = 0;
    for (int i = value; i >= 0; i--) result = result + i;
    return result;
  endfunction
  p: assert property (@(posedge clk) f(a) == 0);
endmodule
