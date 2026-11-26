module level_events(
  input logic clk, a, b, sel, data, mono,
  input logic [3:0] vec,
  input logic [1:0] addr,
  output logic q, mono_q,
  output logic [3:0] bank
);
  function automatic logic mix(input logic x, y);
    return x ^ y;
  endfunction

`ifdef IMPLICIT_REFERENCE
  always @*
`else
  always @(a
`ifndef MISSING_B
           or b
`endif
`ifndef MISSING_GUARD
           or sel
`endif
`ifdef PARTIAL_VECTOR
           or vec[2]
`elsif SPLIT_VECTOR
           or vec[2] or vec[3]
`else
           or vec[3:2]
`endif
`ifndef MISSING_ADDR
           or addr
`endif
           or data
`ifdef MIXED_EDGES
           or posedge clk
`endif
  )
`endif
  begin
`ifdef FLAT_REFERENCE
    q = sel ? mix(a, b) : ^vec[3:2];
`else
    logic tmp;
`ifndef UNASSIGNED_TEMP
    tmp = mix(a, b);
`endif
    if (sel)
      q = tmp;
    else
      q = ^vec[3:2];
`endif
`ifndef DYNAMIC_HOLD
    bank = '0;
`endif
    bank[addr] = data;
  end

  always @(mono)
`ifdef LATCH
    if (mono) mono_q = 1;
`else
    mono_q = mono;
`endif

`ifndef EVENT_SIMULATION
  p_q: assert property (@(posedge clk) q == (sel ? (a ^ b) : ^vec[3:2]));
  p_address: assert property (@(posedge clk) bank == (data ? (4'b0001 << addr) : 4'b0));
  p_single: assert property (@(posedge clk) mono_q == mono);
  p_wrong: assert property (@(posedge clk) q != (sel ? (a ^ b) : ^vec[3:2]));
`endif
endmodule
