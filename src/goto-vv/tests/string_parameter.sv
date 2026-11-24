module select_data #(parameter string INIT = "base.hex")(output wire [7:0] code);
  assign code = INIT == "" ? 8'h42 : 8'h31;
  if (INIT == "image.hex") begin : load_image
    logic [7:0] data [0:1];
    initial $readmemh(INIT, data);
  end
endmodule

module string_parameter(input logic clk);
  wire [7:0] default_code, empty_code;
  select_data from_default(default_code);
  select_data #(.INIT("")) from_empty(empty_code);
  p_value: assert property (@(posedge clk) {default_code, empty_code} == 16'h3142);
endmodule
