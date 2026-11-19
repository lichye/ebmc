module child(input wire [3:0] a); endmodule
module main(input wire [1:0] a); child u(.*); endmodule
