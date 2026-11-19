module main;
  typedef struct packed {logic [2:0] a; logic b;} cfg_t;
  parameter cfg_t ZERO='0, OTHER='{3'd5,1'b1};
  parameter cfg_t UNKNOWN='{3'b1xz,1'b1};
  p0: assert property (ZERO.a==0 && ZERO.b==0 && OTHER.a==5 && OTHER.b);
  p1: assert property (UNKNOWN.a === 3'b1xz && UNKNOWN.b);
endmodule
