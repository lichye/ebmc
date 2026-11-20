module main(input clk, output wire [11:0] bits);
  typedef struct packed {logic [2:0] a; logic [3:0] b;} pair_t;
  typedef struct packed {pair_t p; logic [4:0] c;} state_t;
  state_t q;
  assign bits = q;
  always_ff @(posedge clk)
    q <= '{default: 3, c: 17};
  fields: assert property (@(posedge clk) 1 |=> q.p.a == 3 && q.p.b == 3 && q.c == 17);
  wrong: assert property (@(posedge clk) 1 |=> q.p.b == 0);
endmodule
