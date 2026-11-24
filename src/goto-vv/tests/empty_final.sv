module final_leaf #(parameter string ResultFile = "")(
    input logic phase, output wire [7:0] observed);
  logic [7:0] mem [0:1];
  initial begin
    mem[0] = 8'h39;
    mem[1] = 8'ha6;
  end
  assign observed = mem[phase];

`ifndef OMIT_FINAL
  final begin
    if (ResultFile == "") begin
      ;
      if (ResultFile != "")
        $error("unreachable");
    end else begin
      $writememh(ResultFile, mem);
    end
  end
`endif
endmodule

module final_forward #(parameter string Path = "")(
    input logic phase, output wire [7:0] observed);
  final_leaf #(.ResultFile(Path)) leaf(.phase(phase), .observed(observed));
endmodule

module empty_final(input logic clk);
  logic phase = 1'b0;
  always_ff @(posedge clk) phase <= 1'b1;

  wire [7:0] default_value, forwarded_value;
  final_leaf default_leaf(.phase(phase), .observed(default_value));
  final_forward #(.Path("")) forwarding(.phase(phase), .observed(forwarded_value));

  p_first: assert property (@(posedge clk)
    !phase |-> {default_value, forwarded_value} == 16'h3939);
  p_next: assert property (@(posedge clk)
    phase |-> {default_value, forwarded_value} == 16'ha6a6);
  p_wrong: assert property (@(posedge clk) !phase || default_value != 8'ha6);
endmodule

module final_nonempty(input logic clk);
  wire [7:0] observed;
  final_forward #(.Path("final-guard-dump.hex")) forwarding(
    .phase(1'b0), .observed(observed));
  p: assert property (@(posedge clk) observed == 8'h39);
endmodule
