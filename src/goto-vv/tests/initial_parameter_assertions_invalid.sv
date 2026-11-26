module runtime_value(input logic data);
  initial assert (data) else $fatal(1, "depends on input");
endmodule

module runtime_path(input logic data);
  initial if (data) assert (0) else $fatal(1, "depends on path");
endmodule

module within_event(input logic clk);
  initial @(posedge clk) assert (1) else $fatal(1, "depends on event");
endmodule

module after_event(input logic clk, output logic q);
  initial begin
    begin : event_block
      @(posedge clk) q = 1;
    end
    assert (1) else $fatal(1, "after event");
  end
endmodule

module after_branch_event(input logic clk, input logic data, output logic q);
  initial begin
    if (data) @(posedge clk) q = 1;
    assert (1) else $fatal(1, "after possible event");
  end
endmodule

module after_assignment_delay(output logic q);
  initial begin
    q = #1 1;
    assert (1) else $fatal(1, "after blocking assignment delay");
  end
endmodule

module success_action(output logic q);
  initial assert (1) q = 1; else $fatal(1, "has a success action");
endmodule

module wrong_task;
  initial assert (1) else $finish;
endmodule

module deferred;
  initial assert #0 (1) else $fatal(1, "deferred");
endmodule

module always_runtime(input logic clk);
  always @(posedge clk) assert (1) else $fatal(1, "clocked check");
endmodule
