package arithmetic;
  function automatic int inc(input int n); return n+1; endfunction
endpackage
module local_inc(input int n, output int result);
  import arithmetic::*;
  function automatic int inc(input int x); return x+2; endfunction
  assign result=inc(n);
endmodule
module main(input int n);
  import arithmetic::*;
  int result;
  local_inc u(.n(n), .result(result));
  p0: assert property (inc(n)==n+1 && result==n+2);
endmodule
