module main(input d, en, output y);
`ifdef BIDIRECTIONAL
  `BIDIRECTIONAL (y, d, en);
`elsif NET
  `NET n;
`elsif PULL
  `PULL (y);
`elsif DRIVE
  bufif1 (weak1, strong0) (y, d, en);
`elsif NETSTRENGTH
  wire (strong1, weak0) n = d;
`elsif CHARGE
  trireg (small) n;
`elsif ASSIGNSTRENGTH
  assign (weak1, strong0) y = d;
`elsif PASS
  `PASS (y, d);
`endif
endmodule
