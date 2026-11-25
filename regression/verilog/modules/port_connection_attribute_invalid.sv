module child(input a);
endmodule

module main(input a);
  child broken ((* async = *) .a(a));
endmodule
