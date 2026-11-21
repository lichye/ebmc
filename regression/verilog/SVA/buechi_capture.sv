module attempt #(parameter MODE = 0, OBSERVED = 2, SECOND = 3, STALE = 0)
                (input clk, input [3:0] tick);
  localparam FIRST = MODE == 1 ? 4 : 2;
  localparam END_AT = MODE == 1 ? 12 : MODE == 2 ? 7 : MODE == 3 ? 4 : 3;
  wire start = tick == 0 || (MODE == 2 && tick == 4);
  wire pre = (tick > 0 && tick < FIRST) ||
             (MODE == 2 && tick == 5) || (MODE == 3 && tick == 2);
  wire second = (MODE == 2 && tick == 6) || (MODE == 3 && tick == 3);
  wire take = tick == FIRST || second;
  wire hold = tick > FIRST && tick < END_AT;
  wire [3:0] bus = tick == FIRST ? 2 : second ? SECOND : tick < FIRST ? 1 : 9;
  property check_word;
    bit [3:0] word;
    start ##1 pre[*1:$] ##1 (take, word = STALE ? $past(bus) : bus)
          ##1 hold[*0:$] ##1 (tick == END_AT) |-> word == OBSERVED;
  endproperty
  p: assert property (@(posedge clk) check_word);
endmodule

module main(input clk);
  bit [3:0] tick = 0;
  always_ff @(posedge clk) if (tick != 15) tick <= tick + 1'b1;
  attempt z(clk, tick);
  attempt #(.OBSERVED(9)) wrong(clk, tick);
  attempt #(.STALE(1)) stale(clk, tick);
  attempt #(.MODE(1), .OBSERVED(9)) late(clk, tick);
  // Competing captures 2/3 at the same endpoint. Either expected value fails.
  attempt #(.MODE(2), .OBSERVED(2)) starts2(clk, tick);
  attempt #(.MODE(2), .OBSERVED(3)) starts3(clk, tick);
  attempt #(.MODE(3), .OBSERVED(2)) fork2(clk, tick);
  attempt #(.MODE(3), .OBSERVED(3)) fork3(clk, tick);
  attempt #(.MODE(2), .SECOND(2)) equal_words(clk, tick);
endmodule
