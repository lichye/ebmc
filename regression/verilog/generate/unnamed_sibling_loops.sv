module main(output wire [3:0] result);
  parameter genblk2 = 1;
  if (0) begin : unused end
  generate
    for(genvar i=0; i<2; ++i) begin
      for(genvar j=0; j<2; ++j) begin
        wire value = i == j;
      end
      for(genvar j=0; j<2; ++j) begin
        wire value = i != j;
        assign result[2*i+j] = value;
      end
    end
  endgenerate
  scopes: assert final (result == 4'b0110);
endmodule
