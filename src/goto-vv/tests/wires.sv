module wires(input bit clk);
  wire xval = 1'bx;
  wire zval = 1'bz;
  wire [3:0] partial;
  assign partial[2:1] = 2'b10;
  driven_ok: assert property (@(posedge clk) partial[2:1] == 2'b10);
  floating_same: assert property (@(posedge clk) (partial[0] ^ partial[0]) == 0);
  floating_zero: assert property (@(posedge clk) !partial[3]);
  floating_one: assert property (@(posedge clk) partial[3]);
  x_zero: assert property (@(posedge clk) !xval);
  x_one: assert property (@(posedge clk) xval);
  z_zero: assert property (@(posedge clk) !zval);
  z_one: assert property (@(posedge clk) zval);
endmodule
