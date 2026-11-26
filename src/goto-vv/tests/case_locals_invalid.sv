module case_local_unassigned_selector(input logic a, output logic q);
  always_comb begin
    logic selector;
    if (a) selector = a;
    case (selector)
      1'b1: q = 1;
      default: q = 0;
    endcase
  end
endmodule

module case_local_unassigned_label(input logic a, output logic q);
  always_comb begin
    logic label_value;
    if (a) label_value = a;
    case (a)
      label_value: q = 1;
      default: q = 0;
    endcase
  end
endmodule

module case_local_unassigned_branch(input logic a, output logic q);
  always_comb begin
    logic sign_a;
    if (a) sign_a = a;
    case (a)
      1'b0: q = sign_a;
      default: q = 0;
    endcase
  end
endmodule

module case_local_outside_scope(input logic a, output logic q, r);
  always_comb begin : prepare
    logic scoped;
    scoped = a;
    q = scoped;
  end
  always_comb begin
    logic loaded;
    loaded = a;
    case ({loaded, prepare.scoped})
      2'b00: r = 1;
      default: r = 0;
    endcase
  end
endmodule

module case_local_branch_write(input logic a, b, output logic q);
  always_comb begin
    logic sign_a;
    sign_a = a;
    case (a)
      1'b0: begin
        if (b) sign_a = b;
        q = sign_a;
      end
      default: q = sign_a;
    endcase
  end
endmodule

module case_local_default_write(input logic a, output logic q);
  always_comb begin
    logic sign_a;
    sign_a = a;
    unique case (a)
      1'b0: q = sign_a;
      default: begin
        sign_a = ~a;
        q = sign_a;
      end
    endcase
  end
endmodule

module case_local_wildcard(input logic a, output logic q);
  always_comb begin
    logic sign_a;
    sign_a = a;
    casez (a)
      1'b?: q = sign_a;
    endcase
  end
endmodule

module case_local_priority(input logic a, output logic q);
  always_comb begin
    logic sign_a;
    sign_a = a;
    priority case (a)
      1'b0: q = sign_a;
      default: q = 0;
    endcase
  end
endmodule

module case_local_level_always(input logic a, output logic q);
  always @(a) begin
    logic selector;
    selector = a;
    case (selector)
      1'b1: q = 1;
      default: q = 0;
    endcase
  end
endmodule
