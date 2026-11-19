module main;
  function automatic int f(input int n);
    for (f=0;f<3;f=f+1) begin
      if(f==n) return f+10;
    end
    return 99;
    f=0;
  endfunction
  localparam int a=f(1), b=f(3);
  p0: assert property (a==11 && b==99);
endmodule
