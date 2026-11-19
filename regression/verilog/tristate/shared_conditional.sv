module main(input bit enabled, input bit [11:0] select);
  wire [11:0] control = enabled ? select : 12'bx;

  function automatic [3:0] choose(input [11:0] ctrl);
    reg [3:0] result;
    result = 4'b1000;
    for (int i = 0; i < 12; i = i + 1)
      result = ctrl[i] ? {2'b10, result[0], 1'b1} : result;
    return result;
  endfunction

  wire [3:0] y = choose(control);
  wire [15:0] table_bits = 16'h0100;
  wire selected = table_bits[choose(control)];
  known_index: assert property (enabled && select == 0 |-> selected);
  unknown_index: assert property (!enabled |-> selected);
  common_bits: assert property (y[3:2] == 2'b10);
  all_clear: assert property (enabled && select == 0 |-> y == 4'b1000);
  one_set: assert property (enabled && select == 1 |-> y == 4'b1001);
  unknown_low: assert property (!enabled |-> y[1:0] != 2'b10);
  shared_read: assert property ((y ^ y) == 0);
endmodule
