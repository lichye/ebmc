// Writes with a non-constant index into a combinational variable that
// is read before it is written, as in wdata_transform in PLENA. The
// reads before the writes read the variable itself, and are hence
// non-deterministic (a combinational cycle); since every element is
// written, the result is determined. The value of the variable is bound
// to auxiliary wires, which also carry the reads of the variable.
module main(input [1:0] start, input [63:0] data);

  logic [3:0][15:0] out;

  always @(*) begin
    for (int s = 0; s < 8; s++)
      for (int r = 0; r < 2; r++)
        out[2'(s + start)][r * 8 +: 8] = data[r * 32 + (s % 4) * 8 +: 8];
  end

  // element (c + start) % 4 holds column c of both rows
  function automatic [15:0] expected(input [1:0] element, input [1:0] start, input [63:0] data);
    reg [1:0] c;
    c = element - start;
    expected = {data[32 + c * 8 +: 8], data[c * 8 +: 8]};
  endfunction

  p0: assert final (out[0] == expected(0, start, data));
  p1: assert final (out[1] == expected(1, start, data));
  p2: assert final (out[2] == expected(2, start, data));
  p3: assert final (out[3] == expected(3, start, data));

endmodule
