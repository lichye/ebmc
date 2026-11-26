module effect(input clk_i, output logic result);
  integer selector;
  always @(posedge clk_i) begin
    unique case (selector++)
      0: result <= 1'b0;
      default: result <= 1'b1;
    endcase
  end
endmodule
