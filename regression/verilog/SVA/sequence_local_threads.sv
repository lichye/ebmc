module main(input bit clk, input bit [3:0] a);
  bit [3:0] prev, prev2;
  always_ff @(posedge clk) begin
    prev <= a;
    prev2 <= prev;
  end
  sequence history(value);
    bit [3:0] saved;
    (1, saved = value) ##2 (prev2 == saved);
  endsequence
  property range_capture;
    bit [3:0] first, last;
    (1, first = a) ##[1:2] (1, last = first ^ a) |=> (prev ^ last) == first;
  endproperty
  property init_capture;
    bit [3:0] sampled = a;
    1 |=> prev == sampled;
  endproperty
  sequence initialized_history;
    bit [3:0] saved = a;
    ##2 (prev2 == saved);
  endsequence
  property first_capture;
    bit [3:0] saved;
    first_match(1, saved = a) |=> prev == saved;
  endproperty
  p0: assert property (@(posedge clk) history(a));
  p1: assert property (@(posedge clk) history(~a));
  p2: assert property (@(posedge clk) range_capture);
  p3: assert property (@(posedge clk) init_capture);
  p4: assert property (@(posedge clk) initialized_history);
  p5: assert property (@(posedge clk) first_capture);
endmodule
