module route_diagnostic(
  input bit clk, ready_i, valid_i, locked_route_q,
  input bit [1:0] route_sel_q, route_sel, route_sel_id_q, route_sel_id
);
  always @(posedge clk) begin
    if (ready_i && valid_i && locked_route_q)
      if ((route_sel_id_q != route_sel_id) || (route_sel_q != route_sel))
        $warning("Mismatch in route selection! %d %d", route_sel_id, route_sel);
  end
endmodule

module diagnostic_tasks(input bit clk);
  bit [2:0] step = 0;
  always @(posedge clk) step <= step + 1'b1;
  route_diagnostic route_bad(clk, 1'b1, 1'b1, 1'b1,
    2'd1, step == 1 ? 2'd2 : 2'd1, 2'd1, 2'd1);
  route_diagnostic id_bad(clk, 1'b1, 1'b1, 1'b1,
    2'd1, 2'd1, 2'd1, step == 2 ? 2'd2 : 2'd1);
  route_diagnostic no_mismatch(clk, 1'b1, 1'b1, 1'b1, 2'd1, 2'd1, 2'd2, 2'd2);
  route_diagnostic not_ready(clk, 1'b0, 1'b1, 1'b1, 2'd1, 2'd2, 2'd1, 2'd2);
  route_diagnostic not_valid(clk, 1'b1, 1'b0, 1'b1, 2'd1, 2'd2, 2'd1, 2'd2);
  route_diagnostic not_locked(clk, 1'b1, 1'b1, 1'b0, 2'd1, 2'd2, 2'd1, 2'd2);

  bit [3:0] infos = 0, warnings = 0, observed = 0;
  bit enable_call = 0;
  always @(posedge clk) begin
    enable_call = step == 1;
    if (enable_call) begin
`ifdef DIAGNOSTICS_REFERENCE
      infos++;
      warnings = warnings + 4'd2;
`else
      $info("Count: %d", infos++);
      $warning("Count: %d (%s)", (warnings = warnings + 4'd2), $sformatf("%b", step));
`endif
      observed <= warnings;
    end
    enable_call = 0;
  end
  p_early: assert property (@(posedge clk) step == 1 |->
      infos == 0 && warnings == 0 && observed == 0);
  p_effects: assert property (@(posedge clk) step == 2 |->
      infos == 1 && warnings == 2 && observed == 2);
endmodule

module diagnostic_errors(input bit clk);
  bit [2:0] step = 0;
  always @(posedge clk) begin
    step <= step + 1'b1;
    if (step == 1)
      $warning("first");
    if (step == 2)
      $error("second");
  end
endmodule

module diagnostic_action(input bit clk);
  bit [2:0] step = 0;
  bit [3:0] count = 0;
  always @(posedge clk) begin
    step <= step + 1'b1;
    p_legacy: assert (step != 2) else $error("legacy");
    p_action: assert (step != 1) else $warning("effect %d", count++);
  end
  p_count: assert property (@(posedge clk) step == 2 |-> count == 1);
endmodule

module diagnostic_nested_effect(input bit clk);
  bit [3:0] count = 0;
  always @(posedge clk) $info("nested %d", 1 + count++);
endmodule

module diagnostic_multiple_effects(input bit clk);
  bit [3:0] left_count = 0, right_count = 0;
  always @(posedge clk) $warning("two %d %d", left_count++, right_count++);
endmodule

module diagnostic_unknown_call(input bit clk);
  always @(posedge clk) $info("seed effect %d", $urandom);
endmodule

module diagnostic_fatal(input bit clk);
  always @(posedge clk) $fatal(1, "fatal");
endmodule

module diagnostic_finish(input bit clk);
  always @(posedge clk) $finish;
endmodule

module diagnostic_elaboration_warning(input bit clk);
  $warning("elaboration warning");
  p: assert property (@(posedge clk) clk == clk);
endmodule

module diagnostic_elaboration_error(input bit clk);
  $error("elaboration error");
  p: assert property (@(posedge clk) clk == clk);
endmodule

module diagnostic_final(input bit clk);
  final $warning("output only");
  p: assert property (@(posedge clk) clk == clk);
endmodule

module diagnostic_ref_effect(input bit clk);
  bit [3:0] count = 0;
  function automatic bit [3:0] mutate(ref bit [3:0] target);
    target = target + 1'b1;
    return target;
  endfunction
  always @(posedge clk) $info("ref argument %d", mutate(count));
endmodule

module diagnostic_function_argument(input bit clk);
  bit [3:0] next_step = 0;
  function automatic bit [3:0] plus_one(input bit [3:0] value);
    return value + 1'b1;
  endfunction
  always @(posedge clk) $info("function %d", plus_one(next_step));
  p_unchanged: assert property (@(posedge clk) next_step == 0);
endmodule
