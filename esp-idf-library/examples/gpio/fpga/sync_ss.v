// Sycnronize a signal to a new clock domain using dual flip flops

// Inspired by:
// https://verificationacademy.com/forums/systemverilog/combinationally-sampling-input-clocking-block
module sync_ss(
    input i_clk,
    input i_rst,
    input i_async,
    output reg o_sync);

    reg meta;

    always @(posedge i_clk) begin
        if (i_rst) begin
            meta <= 0;
            o_sync <= 0;
        end
        else begin
            meta <= i_async;
            o_sync <= meta;
        end
    end

endmodule
