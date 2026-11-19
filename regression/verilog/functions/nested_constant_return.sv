package calculators;
  function automatic int inc(input int n); return n+1; endfunction
endpackage
module main;
  function automatic int choose(input int x);
    if(x==0) return calculators::inc(3);
    return calculators::inc(x)+x;
  endfunction
  localparam int a=choose(0), b=choose(4);
  p0: assert property (a==4 && b==9);
endmodule
