module main(input clk, start, take, finish, input [3:0] bus);
  property repeated;
    bit [3:0] word;
    start ##1 ((take, word = bus))[*1:$] ##1 finish |-> bus == word;
  endproperty
  property overwritten;
    bit [3:0] word;
    (start, word = bus) ##1 (take, word = ~bus) ##1 finish |-> bus == word;
  endproperty
  property slice;
    bit [3:0] word;
    (take, word[1:0] = bus[1:0]) ##1 finish |-> bus[1:0] == word[1:0];
  endproperty
  r: assert property (@(posedge clk) repeated);
  w: assert property (@(posedge clk) overwritten);
  s: assert property (@(posedge clk) slice);
endmodule
