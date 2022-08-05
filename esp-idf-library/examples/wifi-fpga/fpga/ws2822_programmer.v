module ws2822_programmer (
    input clk,

    input [15:0] address,
    input program_strobe,

    input [7:0] red,
    input [7:0] green,
    input [7:0] blue,

    input data_strobe,

    output reg power_en,
    output reg data_pin,
    output reg address_pin
);

    assign power_en = 1'b0;
    assign data_pin = 1'b0;
    assign address_pin = 1'b0;

// Programming steps:
// 1. enable power on
// 2. pull data pin low
// 3. DMX out the programming data
// 4. wait
// 5. disable power
// 6. enable power


endmodule
