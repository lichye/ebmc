module main;
  for(genvar h=1;h<9;h*=2) begin:g localparam int v=h; end
  for(genvar k=8;k>0;k>>=1) begin:q localparam int v=k; end
  p0: assert property (g[1].v==1 && g[4].v==4 && q[8].v==8 && q[2].v==2);
endmodule
