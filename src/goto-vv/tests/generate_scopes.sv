module lane #(parameter V = 0)(output logic [3:0] q);
  assign q = V;
endmodule
module generate_scopes(input logic clk);
  wire [3:0] q [0:3];
  for (genvar i=0; i<4; i++) begin : lanes
    if (i % 2 == 0) begin : even
      lane #(.V(i+1)) u(q[i]);
    end else begin : odd
      lane #(.V(i+5)) u(q[i]);
    end
  end
  lanes_ok: assert property (@(posedge clk)
    q[0] == 1 && q[1] == 6 && q[2] == 3 && q[3] == 8);
  wrong_lane: assert property (@(posedge clk) q[2] == 6);
endmodule
