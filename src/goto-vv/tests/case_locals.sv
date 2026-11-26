module case_locals(
  input logic clk, boxed_a, boxed_b, sign_b_in, fill,
  input logic [7:0] operand_a,
  input logic [2:0] mode,
  output logic [7:0] result_bits,
  output logic [1:0] ordered
);
  typedef enum logic [2:0] {RNE=0, RTZ=1, RDN=2, RUP=3} mode_t;
  typedef struct packed {
    logic sign;
    logic [2:0] exponent;
    logic [3:0] mantissa;
  } float_t;

  bit [2:0] phase = 0;
  always_ff @(posedge clk)
    if (phase != 7) phase <= phase + 3'd1;

  wire [6:0] retained = boxed_a ? operand_a[6:0] : 7'h78;
  wire [2:0] rne_mode = boxed_a ? 3'd0 : 3'd4;
  wire a_sign = operand_a[7] & boxed_a;
  wire b_sign = sign_b_in & boxed_b;
  logic after_case;

`ifdef EXPLICIT_REFERENCE
  always_comb begin
    result_bits = {a_sign, retained};
    if (mode == rne_mode)
      result_bits[7] = b_sign;
    else if (mode == RTZ)
      result_bits[7] = ~b_sign;
    else if (mode == RDN)
      result_bits[7] = a_sign ^ b_sign;
    else if (mode == RUP)
      result_bits = operand_a;
    else
`ifdef FOUR_STATE_DEFAULT
      result_bits = {8{1'bx}};
`else
      result_bits = {fill, 2'b00, fill, 3'b000, fill};
`endif
    after_case = a_sign ^ b_sign;
  end
  always_comb begin
    ordered = 0;
    if (phase >= 2) begin
      if (phase == 2) ordered = 1;
      else if (phase >= 4) ordered = 2;
    end
  end
`else
  float_t result;
  always_comb begin : sign_injections
    logic sign_a, sign_b;
    logic [2:0] selector, first_mode;
    result = operand_a;
    if (!boxed_a)
      result = '{sign: 1'b0, exponent: '1, mantissa: 4'b1000};
    sign_a = operand_a[7] & boxed_a;
    sign_b = sign_b_in & boxed_b;
    selector = mode;
    first_mode = rne_mode;

`ifdef ORDINARY_CASE
    case (selector) // sign_case
`else
    unique case (selector) // sign_case
`endif
      first_mode: result.sign = sign_b;
      RTZ: result.sign = ~sign_b;
      RDN: result.sign = sign_a ^ sign_b;
      RUP: result = operand_a;
      default:
`ifdef FOUR_STATE_DEFAULT
        result = '{default: 'x};
`else
        result = '{default: fill};
`endif
    endcase
    sign_a = sign_a ^ sign_b;
    after_case = sign_a;
  end
  assign result_bits = result;

  always_comb begin : select_order
    logic selected, first_label, second_label;
    selected = phase == 2;
    first_label = 1;
    second_label = phase <= 3;
    ordered = 0;
    if (phase >= 2)
`ifdef ORDINARY_CASE
      case (selected) // order_case
`else
      unique case (selected) // order_case
`endif
        first_label: ordered = 1;
        second_label: ordered = 2;
      endcase
  end
`endif

`ifndef EVENT_SIMULATION
  p_rne: assert property (@(posedge clk)
    mode == rne_mode |-> result_bits == {b_sign, retained});
  p_rtz: assert property (@(posedge clk)
    mode == RTZ |-> result_bits == {~b_sign, retained});
  p_rdn: assert property (@(posedge clk)
    mode == RDN |-> result_bits == {a_sign ^ b_sign, retained});
  p_rup: assert property (@(posedge clk)
    mode == RUP |-> result_bits == operand_a);
`ifdef FOUR_STATE_DEFAULT
  p_x_zero: assert property (@(posedge clk) mode == 7 |-> !result_bits[7]);
  p_x_one: assert property (@(posedge clk) mode == 7 |-> result_bits[7]);
`else
  p_default: assert property (@(posedge clk)
    mode != rne_mode && mode != RTZ && mode != RDN && mode != RUP
      |-> result_bits == {fill, 2'b00, fill, 3'b000, fill});
`endif
  p_first_match: assert property (@(posedge clk)
    ordered == (phase == 2 ? 2'd1 : phase >= 4 ? 2'd2 : 2'd0));
  p_after: assert property (@(posedge clk) after_case == (a_sign ^ b_sign));
  p_wrong: assert property (@(posedge clk) phase == 2 |-> ordered != 1);
`endif
endmodule
