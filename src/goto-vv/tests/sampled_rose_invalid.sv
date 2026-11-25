module rose_explicit_clock(input bit clk, other_clk, data);
  check: assert property (@(posedge clk) $rose(data, @(posedge other_clk)));
endmodule

module rose_procedural(input bit clk, data, output bit seen);
  always_ff @(posedge clk) seen <= $rose(data);
endmodule

module rose_function(input bit clk, data);
  function automatic bit rising(input bit sample);
    return $rose(sample);
  endfunction
  check: assert property (@(posedge clk) rising(data));
endmodule

module rose_direct_x(input bit clk);
  check: assert property (@(posedge clk) $rose(1'bx));
endmodule

module rose_direct_z(input bit clk);
  check: assert property (@(posedge clk) $rose(1'bz));
endmodule

module rose_folded_x(input bit clk);
  check: assert property (@(posedge clk) $rose(1'b0 | 1'bx));
endmodule

module rose_folded_z(input bit clk);
  localparam unknown = 1'bz;
  check: assert property (@(posedge clk) $rose(unknown));
endmodule
