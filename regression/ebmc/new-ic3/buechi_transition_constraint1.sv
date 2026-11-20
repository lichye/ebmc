// The Buechi automaton for the negation of p is nondeterministic, and
// its transition relation is a transition constraint of the netlist.
module main(input clk, input a);

  reg a1, a2;

  always @(posedge clk) begin
    a1 <= a;
    a2 <= a1;
  end

  // holds: a2 follows a by two cycles
  p: assert property (@(posedge clk) a |-> ##[1:3] a2);

endmodule
