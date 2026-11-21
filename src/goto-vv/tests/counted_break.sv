module counted_break(input logic clk, input logic [3:0] a);
  logic [2:0] count;
  always_comb begin
    count = 0;
    for(int i=0; i<4; i++) begin
      if(a[3-i]) break;
      count++;
    end
  end
  all_inputs: assert property (@(posedge clk) count ==
    (a[3] ? 0 : a[2] ? 1 : a[1] ? 2 : a[0] ? 3 : 4));
  wrong_count: assert property (@(posedge clk) count != 3);
endmodule
