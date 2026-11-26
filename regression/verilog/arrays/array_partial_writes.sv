// Partial writes into memory words, compared with a reference memory that is
// written with one whole word per cycle, computed in a vector variable.
//  A: byte enables and an overlapping write; the later write wins
//  B: guarded bit writes, one per bit, as Yosys writes a masked port
//  C: a write at a and a part write at a2, equal or not at runtime
//  E: blocking writes, a read of the written element, and a read after
//  F: nonblocking writes and a read in the same cycle (sees the old word)
//  G: read-modify-write of bits of the same word
module main(input clk, input logic [1:0] a, input logic [1:0] a2,
            input logic [3:0] e, input logic [15:0] d, input logic [15:0] d2,
            input logic [15:0] m);
  logic [15:0] A [3:0], Ar [3:0], B [3:0], Br [3:0], C [3:0], Cr [3:0];
  logic [15:0] E [3:0], Er [3:0], F [3:0], Fr [3:0], G [3:0], Gr [3:0];
  logic [15:0] qe, qer, qf, qfr;
  initial for (int k = 0; k < 4; k++) begin
    A[k] = 0; Ar[k] = 0; B[k] = 0; Br[k] = 0; C[k] = 0; Cr[k] = 0;
    E[k] = 0; Er[k] = 0; F[k] = 0; Fr[k] = 0; G[k] = 0; Gr[k] = 0;
  end
  initial begin qe = 0; qer = 0; qf = 0; qfr = 0; end

  always_ff @(posedge clk) begin
    if (e[0]) A[a][7:0] <= d[7:0];
    if (e[1]) A[a][15:8] <= d[15:8];
    if (e[2]) A[a][11:4] <= d2[7:0];
  end
  always_ff @(posedge clk) begin
    logic [15:0] w;
    w = Ar[a];
    if (e[0]) w[7:0] = d[7:0];
    if (e[1]) w[15:8] = d[15:8];
    if (e[2]) w[11:4] = d2[7:0];
    Ar[a] <= w;
  end

  always_ff @(posedge clk)
    for (int b = 0; b < 16; b++)
      if (m[b]) B[a][b] <= d[b];
  always_ff @(posedge clk) Br[a] <= (Br[a] & ~m) | (d & m);

  always_ff @(posedge clk) begin
    if (e[0]) C[a] <= d;
    if (e[1]) C[a2][3:0] <= d2[3:0];
  end
  always_ff @(posedge clk)
    for (int k = 0; k < 4; k++) begin
      logic [15:0] w;
      w = Cr[k];
      if (e[0] && a == k) w = d;
      if (e[1] && a2 == k) w[3:0] = d2[3:0];
      Cr[k] <= w;
    end

  always_ff @(posedge clk) begin
    if (e[0]) E[a][3:0] = d[3:0];
    if (e[1]) E[a][7:4] = E[a][3:0];
    if (e[2]) E[a2][15:12] = d2[3:0];
    qe <= E[a];
  end
  always_ff @(posedge clk) begin
    logic [15:0] w, w2;
    w = Er[a];
    if (e[0]) w[3:0] = d[3:0];
    if (e[1]) w[7:4] = w[3:0];
    Er[a] = w;
    w2 = Er[a2];
    if (e[2]) w2[15:12] = d2[3:0];
    Er[a2] = w2;
    qer <= Er[a];
  end

  always_ff @(posedge clk) begin
    if (e[0]) F[a][3:0] <= d[3:0];
    if (e[1]) F[a][15:4] <= d[15:4];
    qf <= F[a];
  end
  always_ff @(posedge clk) begin
    logic [15:0] w;
    w = Fr[a];
    if (e[0]) w[3:0] = d[3:0];
    if (e[1]) w[15:4] = d[15:4];
    Fr[a] <= w;
    qfr <= Fr[a];
  end

  always_ff @(posedge clk) begin
    if (e[0]) G[a][0] <= ~G[a][0];
    if (e[1]) G[a][1] <= G[a][0];
    if (e[2]) G[a][2] <= G[a][1] ^ d[2];
  end
  always_ff @(posedge clk) begin
    logic [15:0] w;
    w = Gr[a];
    if (e[0]) w[0] = ~Gr[a][0];
    if (e[1]) w[1] = Gr[a][0];
    if (e[2]) w[2] = Gr[a][1] ^ d[2];
    Gr[a] <= w;
  end

  p_A: assert property (@(posedge clk) A[0] == Ar[0] && A[1] == Ar[1] && A[2] == Ar[2] && A[3] == Ar[3]);
  p_B: assert property (@(posedge clk) B[0] == Br[0] && B[1] == Br[1] && B[2] == Br[2] && B[3] == Br[3]);
  p_C: assert property (@(posedge clk) C[0] == Cr[0] && C[1] == Cr[1] && C[2] == Cr[2] && C[3] == Cr[3]);
  p_E: assert property (@(posedge clk) E[0] == Er[0] && E[1] == Er[1] && E[2] == Er[2] && E[3] == Er[3] && qe == qer);
  p_F: assert property (@(posedge clk) F[0] == Fr[0] && F[1] == Fr[1] && F[2] == Fr[2] && F[3] == Fr[3] && qf == qfr);
  p_G: assert property (@(posedge clk) G[0] == Gr[0] && G[1] == Gr[1] && G[2] == Gr[2] && G[3] == Gr[3]);
  r_A: assert property (@(posedge clk) A[1] == 0);
  r_C: assert property (@(posedge clk) C[2] == 0);
  r_G: assert property (@(posedge clk) G[3] == 0);
endmodule
