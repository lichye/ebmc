module main(input d, en, other);
  trireg charge;
  bufif1 (charge, d, en);
  bufif1 (charge, !d, other);
  transparent: assert property ((en && !other) |-> charge == d);
  conflict_zero: assert property ((en && other) |-> !charge);
  conflict_one: assert property ((en && other) |-> charge);
  // Includes release immediately after conflicting active drivers.
  held: assert property (1'b1 |=> (en || other || $stable(charge)));
  trireg initial_charge;
  initial_free0: assert property (!initial_charge);
  initial_free1: assert property (initial_charge);
  initial_held: assert property (1'b1 |=> $stable(initial_charge));
endmodule
