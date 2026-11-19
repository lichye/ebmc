module main(
  input bit [3:0] bits,
  input logic [3:0] logic_bits,
  input bit [1:0] narrow,
  input logic [2:5] ascending,
  input bit signed [79:0] large_index,
  input [3:0] implicit_net,
  input logic [0:0] flag,
  input integer count,
  input time timestamp,
  input int i
);
  wire [5:0] wide = bits[i +: 6];
  wire [5:0] below = bits[narrow -: 6];
  p0: assert property (i == -2 |-> wide == {bits, 2'b00});
  p1: assert property (i == 0 |-> wide == {2'b00, bits});
  p2: assert property (i == 2 |-> wide == {4'b0000, bits[3:2]});
  p3: assert property (i >= 4 || i < -5 |-> wide == 0);
  p4: assert property (narrow == 1 |-> below == {bits[1:0], 4'b0000});

  p5: assert property (
    i == -2 |-> logic_bits[i +: 6] === {logic_bits, 2'bxx});
  p6: assert property (
    i == 2 |-> logic_bits[i +: 6] === {4'bxxxx, logic_bits[3:2]});
  p7: assert property (
    i >= 4 || i < -5 |-> logic_bits[i +: 6] === 6'bxxxxxx);

  p8: assert property (logic_bits[-1 +: 4] === {logic_bits[2:0], 1'bx});
  p9: assert property (logic_bits[4 -: 4] === {1'bx, logic_bits[3:1]});
  p10: assert property (
    large_index == (80'sd1 <<< 70) + 1 |->
      bits[large_index +: 3] == 0 && logic_bits[large_index -: 3] === 3'bxxx);
  p11: assert property (
    large_index == -(80'sd1 <<< 70) + 1 |->
      bits[large_index -: 3] == 0 && logic_bits[large_index +: 3] === 3'bxxx);
  p12: assert property (i == 1 |-> ascending[i +: 3] === {1'bx, ascending[2:3]});
  p13: assert property (i == 6 |-> ascending[i -: 3] === {ascending[4:5], 1'bx});
  p14: assert property (bits[1'bx +: 3] == 0 && logic_bits[1'bx +: 3] === 3'bxxx);
  localparam logic [2:5] pat = 4'bz1x0;
  p15: assert property (i == 3 |-> pat[i +: 3] === 3'b1x0);
  p16: assert property (i == 1 |-> pat[i +: 3] === 3'bxz1);
  p17: assert property (i == 10 |-> implicit_net[i +: 1] === 1'bx && flag[i +: 1] === 1'bx);
  p18: assert property (i == 80 |-> count[i +: 1] === 1'bx && timestamp[i +: 1] === 1'bx);
endmodule
