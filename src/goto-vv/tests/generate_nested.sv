module lane_maintenance #(parameter int VALUE = 0)(output logic [3:0] q);
  assign q = VALUE;
endmodule

module generate_nested(input logic clk);
  wire [3:0] q[0:3];
  if (1) begin : outer
    for (genvar i = 0; i != 4; i++) begin : lanes
      if (i % 2 == 0) begin : even_lane
        lane_maintenance #(.VALUE(i+1)) u(q[i]);
      end else begin : odd_lane
        lane_maintenance #(.VALUE(i+5)) u(q[i]);
      end
      if (0) begin : unused_lane
        wire unused;
        assign unused = 1'b1;
      end
    end
  end
  p_order: assert property (@(posedge clk) q[0] == 1 && q[1] == 6 && q[2] == 3 && q[3] == 8);
  p_wrong: assert property (@(posedge clk) q[2] == 6);
endmodule
