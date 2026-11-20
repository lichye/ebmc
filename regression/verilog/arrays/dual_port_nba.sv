module main(input wire clk, ea, eb, a, b, input wire [1:0] da, db);
  reg [1:0] mem[0:1];
  reg [1:0] read_a, old0, old1;
  always @(posedge clk) begin
    if(ea) mem[a] <= da;
    read_a <= mem[a];
  end
  always @(posedge clk)
    if(eb) mem[b] <= db;

  reg valid = 0;
  reg last_a, last_b, last_ea, last_eb;
  reg [1:0] last_da, last_db;
  always @(posedge clk) begin
    valid <= 1;
    {last_a,last_b,last_ea,last_eb} <= {a,b,ea,eb};
    {last_da,last_db} <= {da,db};
    {old0,old1} <= {mem[0],mem[1]};
  end
  wire collision = last_ea && last_eb && last_a == last_b;
  port_a: assert final (!valid || !last_ea || collision || mem[last_a] == last_da);
  port_b: assert final (!valid || !last_eb || collision || mem[last_b] == last_db);
  hold0: assert final (!valid || (last_ea && !last_a) || (last_eb && !last_b) || mem[0] == old0);
  hold1: assert final (!valid || (last_ea && last_a) || (last_eb && last_b) || mem[1] == old1);
  atomic_word: assert final (!valid || !collision || mem[last_a] == last_da || mem[last_a] == last_db);
  before_write: assert final (!valid || read_a == (last_a ? old1 : old0));
  only_a: assert final (!valid || !collision || mem[last_a] == last_da);
  only_b: assert final (!valid || !collision || mem[last_b] == last_db);
endmodule
