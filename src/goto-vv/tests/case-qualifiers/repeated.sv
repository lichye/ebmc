module repeated(input clk_i, input [1:0] op, output logic [3:0] result);
  function automatic [3:0] select_value(input [1:0] selector);
    unique case (selector)
      2'd0: return 4'd1;
      default: return 4'd2;
    endcase
  endfunction
  always @(posedge clk_i) result <= select_value(op);
endmodule
