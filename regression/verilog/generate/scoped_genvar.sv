module main(input bit [1:0] a);
  for(genvar i=0;i<2;i++) begin:g wire b=a[i]; end
  for(genvar i=1;i<3;i++) begin:h wire b=a[i-1]; end
  p0: assert property (g[0].b==h[1].b && g[1].b==h[2].b);
endmodule
