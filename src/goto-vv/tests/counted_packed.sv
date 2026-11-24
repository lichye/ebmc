module counted_packed(input logic clk, select_input);
`ifdef NATIVE_FLAT_LOOP_REFERENCE
  wire [47:0] source = select_input ? 48'hE4311676A14A : 48'h78169A4025B3;
  `define COUNTED_LANE(p,g) source[((p)*2+(g))*8 +: 8]
`else
  wire [2:0][1:0][7:0] source = select_input ? 48'hE4311676A14A : 48'h78169A4025B3;
  `define COUNTED_LANE(p,g) source[p][g]
`endif
  logic [1:0][7:0] maxima, minima;
  logic [1:0][15:0] accumulated;

  for (genvar g = 0; g < 2; g++) begin : g_lane
    logic [7:0] highest, lowest;
    logic [15:0] acc;
    always_comb begin
      highest = `COUNTED_LANE(0,g);
      lowest = `COUNTED_LANE(0,g);
      for (int p = 1; p < 3; p++) begin
        if (`COUNTED_LANE(p,g) > highest) highest = `COUNTED_LANE(p,g);
        if (`COUNTED_LANE(p,g) < lowest) lowest = `COUNTED_LANE(p,g);
      end
      acc = 0;
      for (int p = 0; p < 3; p++) begin
        acc = acc * 16'd3 + `COUNTED_LANE(p,g);
      end
    end
    assign maxima[g] = highest;
    assign minima[g] = lowest;
    assign accumulated[g] = acc;
  end
  `undef COUNTED_LANE

  p_max: assert property (@(posedge clk)
    maxima == (select_input ? 16'hE476 : 16'h9AB3));
  p_min: assert property (@(posedge clk)
    minima == (select_input ? 16'h1631 : 16'h2516));
  p_acc: assert property (@(posedge clk)
    accumulated == (select_input ? 32'h06CF042D : 32'h03930721));
  p_wrong_first: assert property (@(posedge clk)
    !select_input |-> accumulated != 32'h03930721);
  p_wrong_second: assert property (@(posedge clk)
    select_input |-> accumulated != 32'h06CF042D);
endmodule
