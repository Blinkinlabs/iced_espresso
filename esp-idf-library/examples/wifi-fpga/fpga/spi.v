module spi (
    input i_clk,
    input i_rst,

    // SPI bus connection. These are in the SCK clock domain.
    input i_cs,
    input i_sck,
    input i_mosi,
    output reg o_miso,

    // System bus connections. These are in the system clock domain.
    output reg [7:0] o_command,     // Transaction command
    output reg [15:0] o_address,    // Transaction address

    output wire o_transaction_strobe,     // Asserts for 1 system clock cycle when a write is requested
    output reg [15:0] o_write_data, // Data to write to FPGA memory
    input [15:0] i_read_data       // Data read from FPGA memory
);

// System data register to SPI input

    reg [15:0] read_data_sync;
    reg [15:0] read_data_current_word;

    // TODO sync this across the clock boundary
    always @(posedge i_sck) begin
        read_data_sync <= i_read_data;
    end

// SPI to sytem output

    // Toggle signal for write (in CIN clock domain)
    reg transaction_toggle;
    wire transaction_toggle_sync;

    // Synchronize o_transaction_strobe to system clock
    sync_ss din_sync_ss_1(
        .i_clk(i_clk),
        .i_rst(i_rst),
        .i_async(transaction_toggle),
        .o_sync(transaction_toggle_sync)
    );

    // Convert the toggle to a strobe, in system clock domain
    toggle_to_strobe toggle_to_strobe_1(
        .i_clk(i_clk),
        .i_rst(i_rst),
        .i_toggle(transaction_toggle_sync),
        .o_strobe(o_transaction_strobe)
    );

    reg [3:0] bit_index;

    localparam STATE_RX_COMMAND = 0;
    localparam STATE_RX_ADDRESS = 1;
    localparam STATE_RX_FIRST_WORD = 2;
    localparam STATE_RX_MORE_WORDS = 3;
    localparam STATE_TX_PRE_DELAY = 4;
    localparam STATE_TX = 5;

    reg [2:0] state;

    // Buffer to receive MOSI data into (in CIN clock domain)
    // Note: This is 1 bit shorter than the registers it is buffering to,
    // because the last bit is directly read from the mosi pin.
    reg [14:0] rx_buffer;

    // The last bit of the rx_buffer is read directly from MOSI
    wire [15:0] rx_data = {rx_buffer, i_mosi};

    // Bit 0 of the command is the r/w flag (other bits are reserved)
    wire command_write = o_command[0];

    // For simulation
    initial begin
        o_miso = 0;

        o_write_data = 0;
        o_command = 0;
        o_address = 0;

        rx_buffer = 0;
        transaction_toggle = 0;

        bit_index = 0;
        state = 0;
    end

    // By inspection, there is a 14ps delay between the clock transition and
    // the bit change propigating back out
    always @(negedge i_sck or posedge i_cs) begin
        if(i_cs) begin
            o_miso <= 0;
        end
        else begin
            if(state == STATE_TX) begin
                o_miso <= read_data_current_word[bit_index];
            end
        end
    end

    always @(posedge i_sck or posedge i_cs) begin
        if(i_cs) begin
            bit_index <= 7;
            state <= STATE_RX_COMMAND;
        end
        else begin
            rx_buffer <= rx_data[14:0];
            bit_index <= bit_index - 1;

            // Once enough bits have been transferred for the current cycle
            if(bit_index == 0) begin
                bit_index <= 15;

                case(state)
                    STATE_RX_COMMAND:
                    begin
                        o_command <= rx_data[7:0];
                        state <= STATE_RX_ADDRESS;
                    end
                    STATE_RX_ADDRESS:
                    begin
                        o_address <= rx_data;

                        if(command_write == 1) begin
                            state <= STATE_RX_FIRST_WORD;
                        end
                        else begin
                            // 16 cycle pre-delay for first read, to give the
                            // toggle time to propigate across the clock
                            // boundary
                            bit_index <= 7;
                            state <= STATE_TX_PRE_DELAY;
                            transaction_toggle <= ~transaction_toggle;
                        end
                    end
                    STATE_RX_FIRST_WORD:
                    begin
                        o_write_data <= rx_data;
                        transaction_toggle <= ~transaction_toggle;
                        state <= STATE_RX_MORE_WORDS;
                    end
                    STATE_RX_MORE_WORDS:
                    begin
                        o_write_data <= rx_data;
                        o_address <= o_address + 1; // TODO: address should be incremented before toggle is applied?
                        transaction_toggle <= ~transaction_toggle;
                    end
                    STATE_TX_PRE_DELAY:
                    begin
                        state <= STATE_TX;

                        o_address <= o_address + 1;
                        transaction_toggle <= ~transaction_toggle;
                        read_data_current_word <= read_data_sync;
                    end
                    STATE_TX:
                    begin
                        o_address <= o_address + 1;
                        transaction_toggle <= ~transaction_toggle;
                        read_data_current_word <= read_data_sync;
                    end
                endcase
            end
        end
    end
endmodule
