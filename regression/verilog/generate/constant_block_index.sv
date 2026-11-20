module main(output wire y);
  for(genvar i=0; i<2; ++i) begin : rows
    wire value;
    if(i == 0) assign value=1'b1;
    else assign value=rows[i-1].value;
  end
  assign y=rows[1].value;
  correct: assert final (y);
endmodule
