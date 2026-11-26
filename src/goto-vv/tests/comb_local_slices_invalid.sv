module unassigned_word(input logic [1:0] in, output logic [3:0] result);
  always_comb begin
    automatic logic [3:0] bits;
    bits[1:0] = in;
    result = bits;
  end
endmodule

module unassigned_slice(input logic [1:0] in, output logic [1:0] result);
  always_comb begin
    automatic logic [3:0] bits;
    bits[1:0] = in;
    result = bits[2:1];
  end
endmodule

module missing_path(input logic choice, output logic result);
  always_comb begin
    automatic logic [3:0] bits;
    if (choice)
      bits[1:0] = 2'b01;
    else
      bits[2:1] = 2'b01;
    result = bits[0];
  end
endmodule

module overlapping_read(input logic in, output logic [1:0] result);
  always_comb begin
    automatic logic [4:2] bits;
    bits[2] = in;
    bits[4:3] = bits[3:2];
    result = bits[4:3];
  end
endmodule

module bad_target(input logic [1:0] index, input logic in, output logic [3:0] result);
  always_comb begin
`ifdef STATIC_LOCAL
    static logic [3:0] bits;
`else
    automatic logic [3:0] bits;
`endif
    bits = '0;
`ifdef OUTSIDE
    bits[4] = in;
`elsif DYNAMIC_RANGE
    bits[index +: 2] = {2{in}};
`elsif NBA
    bits[1] <= in;
`elsif CASE_WRITE
    case (in)
      1'b1: bits[1] = in;
      default: ;
    endcase
`elsif STATIC_LOCAL
    bits[1] = in;
`else
    bits[index] = in;
`endif
    result = bits;
  end
endmodule
