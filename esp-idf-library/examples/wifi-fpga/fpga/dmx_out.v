module dmx_out #(
) (
    input i_clk,
    input i_rst,

    input [7:0] i_write_addr,       // (word) address to write to
    input [15:0] i_write_data,      // 16-bit data to write
    input i_write_strobe,           // Write strobe

    input [9:0] i_channel_count,      // Number of channels (0-512)
    input i_start_strobe,             // Start whenever this input toggles

    output reg o_busy,                // High when the DMX output is active
    output reg o_data             // DMX output signal
);
    // DMX memory
    wire [7:0] matrix_1_raddr;
    wire [15:0] matrix_1_rdata;

    SB_RAM40_4K dmx_memory (
        .RDATA(matrix_1_rdata),
        .RADDR({3'd0, matrix_1_raddr}),
        .RCLK(i_clk),
        .RCLKE(1'b1),
        .RE(1'b1),
        
        .WADDR({3'd0, i_write_addr}),
        .WCLK(i_clk),
        .WCLKE(1'b1),
        .WDATA(i_write_data),
        .WE(i_write_strobe),
        .MASK(1'b0)
    );

    // DMX protocol
    //
    // Notes on DMX signal:
    // 1. Line is high when idle
    // 2. Transmission starts by pulling line low to begin 'break' for minimum 92uS
    //    (typical 176uS).
    // 3. Next, pull line high to begin 'mark after break' for minimum 12uS, max
    //    1M uS.
    // 4. Slot 0 is 1 start bit 1'b0, 8 data bits equalling 8'b00000000, 2 stop
    //    bits 2'b11. Each slot is 4uS.
    // 5. An inter-frame delay of 0-? uS.
    // 5. Up to 512 data slot bits, each with 1 start bit 1'b0, data 8'bxxxxxxxx,
    //    2 stop bits 2'b11, and each seperated by an inter-frame delay.
    // 6. Frame complete after 512 channels received, or when next 'break'
    //    detected.
    //
    //    Assume a 24 MHz input clock, divided by 4 to make a 6 MHz clock
    //localparam BREAK_COUNT = (1056-1);  // Clock cycles for break (176uS)
    localparam BREAK_COUNT = (30000-1);  // Clock cycles for break (5mS)
    localparam MAB_COUNT = (72-1);     // Clock cycles for 'MAB' (12uS)
    //localparam MAB_COUNT = (528-1);     // Clock cycles for 'MAB' (88uS)
    localparam BIT_COUNT = (24-1);      // Clock cycles for a bit (4uS)

    localparam STATE_START = 0;
    localparam STATE_BREAK = 1;
    localparam STATE_MARK_AFTER_BREAK = 2;
    localparam STATE_STARTCODE = 3;
    localparam STATE_CHANNEL = 4;

    reg [7:0] data [0:511];
    initial begin
        //data[0] = 8'h80;//01;
        data[0] = 8'hE0;//04;
        data[1] = 8'h0f;//f0;
        data[2] = 8'h4b;//d2;
        //data[2] = 8'h75;//ae;

        data[3] = 8'd255;
        data[4] = 8'd0;
        data[5] = 8'd0;

        data[6] = 8'd0;
        data[7] = 8'd255;
        data[8] = 8'd0;

        data[9] = 8'd0;
        data[10] = 8'd0;
        data[11] = 8'd255;
    end

    reg [2:0] state;                // State machine 
    //reg [12:0] counter;             // General purpose counter
    reg [24:0] counter;             // General purpose counter
    reg [9:0] channels_written;     // Number of channels written
    reg [5:0] bit_index;            // Bit we are currently clocking out
    initial begin
        state = STATE_START;
    end

    assign matrix_1_raddr = channels_written[8:1];

    reg [7:0] val;                  // Current channel data

    localparam DMX_START_CODE_LENGTH = (1+8+2);
    wire [(DMX_START_CODE_LENGTH-1):0] dmx_start_code = {11'b11000000000};

    localparam DMX_DATA_LENGTH = (1+8+2);   // 1 start bit, 8 data bits, 2 stop bits
    wire [(DMX_DATA_LENGTH-1):0] dmx_data = {2'b11, val[7:0], 1'b0};

    // Divide the 24mhz input clock to 6MHz
    reg [2:0] clk_div;
    reg [2:0] strobe_countdown;
    initial begin
        clk_div = 3'd0;
        strobe_countdown = 3'd0;
    end

    always @(posedge i_clk) begin
        clk_div <= clk_div + 1;

        if(strobe_countdown > 0)
            strobe_countdown <= strobe_countdown - 1;

        if(i_start_strobe)
            strobe_countdown <= 3'b111;
    end
    wire pixel_clock = clk_div[1];

    wire start = (strobe_countdown != 3'd0);

    always @(posedge pixel_clock) begin
        o_data <= 1;
        counter <= counter - 1;
        o_busy <= 1;

        if(i_rst) begin
            state <= STATE_START;
        end
        else begin
            case(state)
            STATE_START:
            begin
                o_busy <= 0;

                if(start) begin
                    state <= STATE_BREAK;
                    counter <= BREAK_COUNT;
                end
            end
            STATE_BREAK:
            begin
                o_data <= 0;

                if(counter == 0) begin
                    state <= STATE_MARK_AFTER_BREAK;
                    counter <= MAB_COUNT;

                    channels_written <= 0;
                end
            end
            STATE_MARK_AFTER_BREAK:
            begin
                if(counter == 0) begin
                    state <= STATE_STARTCODE;
                    counter <= BIT_COUNT;

                    channels_written <= channels_written + 1;
                    if(channels_written[0] == 1)
                        val <= matrix_1_rdata[7:0];
                    else
                        val <= matrix_1_rdata[15:8];

                    bit_index <= 0;
                end
            end
            STATE_STARTCODE:
            begin
                o_data <= dmx_start_code[bit_index]; 

                if(counter == 0) begin
                    counter <= BIT_COUNT;
                    bit_index <= bit_index + 1;

                    if(bit_index == (DMX_START_CODE_LENGTH-1)) begin
                        state <= STATE_CHANNEL;
                        bit_index <= 0;
                    end
                end
                
            end
            STATE_CHANNEL:
            begin
                o_data <= dmx_data[bit_index]; 

                if(counter == 0) begin
                    counter <= BIT_COUNT;
                    bit_index <= bit_index + 1;

                    if(bit_index == (DMX_DATA_LENGTH-1)) begin
                        channels_written <= channels_written + 1;
                        //val <= matrix_1_rdata[7:0];
                        if(channels_written[0] == 1)
                            val <= matrix_1_rdata[7:0];
                        else
                            val <= matrix_1_rdata[15:8];

                        bit_index <= 0;

                        if(channels_written == i_channel_count) begin
                            state <= STATE_START;
                        end
                    end
                end
            end

            default:
            begin
                state <= STATE_START;
            end
            endcase
        end
    end
endmodule

