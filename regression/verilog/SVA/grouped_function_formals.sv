module main(input bit [3:0] a,b);
  function automatic logic [3:0] f(input logic [3:0] x,y);
    if(x==0) return y;
    return x;
  endfunction
  p0: assert property (f(a,b)==(a==0 ? b : a));
endmodule
