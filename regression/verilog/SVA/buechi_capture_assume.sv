module main(input clk, start, take, finish, input [3:0] bus);
  property contract;
    bit [3:0] word;
    start ##1 (take, word = bus) ##1 finish |-> bus == word;
  endproperty
  env: assume property (@(posedge clk) contract);
  check_env: assert property (@(posedge clk) !finish);
endmodule
