module read_before(input logic clk, output logic [4:0] result);
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;
  always_comb begin
    automatic status_t temp_status;
    result = temp_status;
  end
endmodule

module or_before(input logic clk, input logic [4:0] data, output logic [4:0] result);
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;
  always_comb begin
    automatic status_t temp_status;
    temp_status |= data;
    result = temp_status;
  end
endmodule

module partial_path(input logic clk, valid, input logic [4:0] data,
                    output logic [4:0] result);
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;
  always_comb begin
    automatic status_t temp_status;
    if (valid) temp_status = '0;
    temp_status |= data;
    result = temp_status;
  end
endmodule

`ifdef EXTERNAL_READ
module external_read(input logic clk, input logic [4:0] data,
                     output logic [4:0] result, leaked);
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;
  always_comb begin : output_processing
    automatic status_t temp_status;
    temp_status = data;
    result = temp_status;
  end
  assign leaked = output_processing.temp_status;
endmodule

`endif

module static_value(input logic clk, input logic [4:0] data,
                    output logic [4:0] result);
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;
  always_comb begin
    static status_t temp_status;
    temp_status = data;
    result = temp_status;
  end
endmodule

module partial_write(input logic clk, data, output logic [4:0] result);
  typedef struct packed {logic nv, dz, of, uf, nx;} status_t;
  always_comb begin
    automatic status_t temp_status;
    temp_status = '0;
    temp_status.nv = data;
    result = temp_status;
  end
endmodule
