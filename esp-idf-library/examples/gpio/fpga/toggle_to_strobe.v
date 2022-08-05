module toggle_to_strobe(
    input i_clk,
    input i_rst,

    input i_toggle,
    output reg o_strobe
);
    reg i_toggle_last;

    always @(posedge i_clk) begin
        if(i_rst) begin
            // TODO: If i_toggle is not 0 initally, we emit a spurious strobe when coming out of reset.
            i_toggle_last <= 0;
            o_strobe <= 0;
        end
        else begin
            i_toggle_last <= i_toggle;
            o_strobe <= 0;

            if(i_toggle != i_toggle_last)
                o_strobe <= 1;
        end

    end

endmodule
