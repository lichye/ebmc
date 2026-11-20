module main(input bit clk, input bit [23:0] a);
  function automatic bit parity(input bit [23:0] x, input int width);
    parity = 0;
    for (int i = 0; i < width; i++)
      if (x[i])
        parity = !parity;
  endfunction

  p_parity: assert property (a[23:8] == 'hac53 |-> parity(a, 24) == ^a);
  p_past: assert property (@(posedge clk)
    a[23:8] == 'hac53 |=> parity($past(a), 24) == $past(^a));
  p_changes: assert property (@(posedge clk)
    a[23:8] == 'hac53 |=> parity($past(a), 24) == ^a);

  // Overlapping matches carry independent snapshots of the low ten bits.
  property captured_parity;
    bit [9:0] saved;
    @(posedge clk) (1, saved = a[9:0]) |=> parity(saved, 10) == $past(^a[9:0]);
  endproperty
  p_capture: assert property (captured_parity);
endmodule
