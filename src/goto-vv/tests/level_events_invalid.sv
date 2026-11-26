module derived_event(input logic clk, a, b, output logic q);
  always @(a && b) q = a ^ b;
  p: assert property (@(posedge clk) q == (a ^ b));
endmodule

module dynamic_event(input logic clk, input logic [3:0] vec,
                     input logic [1:0] index, output logic q);
  always @(vec[index] or index) q = vec[index];
  p: assert property (@(posedge clk) q == vec[index]);
endmodule

module partial_array(input logic clk, input logic [3:0] mem[0:1],
                     input logic index, output logic [3:0] q);
  always @(mem[0] or index) q = mem[index];
  p: assert property (@(posedge clk) q == mem[index]);
endmodule

module hidden_read(input logic clk, a, extra, output logic q);
  function automatic logic inner(input logic x);
    return x ^ extra;
  endfunction
  function automatic logic outer(input logic x);
    return inner(x);
  endfunction
  always @(a) q = outer(a);
  p: assert property (@(posedge clk) q == (a ^ extra));
endmodule

module timed_write(input logic clk, a, output logic q);
  always @(a) q = #1 a;
  p: assert property (@(posedge clk) q == a);
endmodule

module self_read(input logic clk, a, output logic q);
  always @(a or q) q = a ^ q;
  p: assert property (@(posedge clk) q == a);
endmodule

module constant_body(input logic clk, a, output logic q);
  always @(a) q = 1;
  p: assert property (@(posedge clk) q);
endmodule
