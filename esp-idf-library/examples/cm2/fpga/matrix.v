module matrix (
    input i_clk,

    output reg o_led_oe,
    output reg o_led_clk,
    output reg o_led_lat,
    output reg o_led_red1,
    output reg o_led_red2,

    output [7:0] o_raddr_1,
    input [15:0] i_rdata_1,
    output [7:0] o_raddr_2,
    input [15:0] i_rdata_2
);


    //########### ICND2026 driver ##############################################
    
    // 1. Clock out 16*16 bits
    // 2. Pull OE high
    // 3. Pull LE low
    // 4. Pull LE high
    // 5. Pull OE low

    reg [16:0] delay;

    reg [8:0] led;      // LED being drawn to [0-271]
    reg [3:0] pwm_bit;
    reg [2:0] state;

    initial begin
        delay = 17'd0;
        led = 9'd0;
        state = 3'd0;
        pwm_bit = 4'd0;

        o_led_oe = 1'b1;
    end

    localparam STATE_READY = 3'd0;
    localparam STATE_PRE_DELAY = 3'd1;
    localparam STATE_WRITING = 3'd2;
    localparam STATE_WAIT_OE = 3'd3;
    localparam STATE_LATCH = 3'd4;
    localparam STATE_DELAY = 3'd5;

    reg [15:0] pwm_lut_1 [255:0];
    initial begin
        $readmemh("lut_8_to_16_pow_1.80.list", pwm_lut_1);
    end

    reg [15:0] pwm_lut_2 [255:0];
    initial begin
        $readmemh("lut_8_to_16_pow_1.80.list", pwm_lut_2);
    end

    // Invert the lowest 16 bits, to match the hardware layout
    // 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,31,30...
    wire [7:0] current_led = {led[7:4],~led[3:0]};
    assign o_raddr_1 = current_led;
    assign o_raddr_2 = current_led;

    // Flip byte endianness
    wire [15:0] led_data_1 = {i_rdata_1[7:0], i_rdata_1[15:8]};
    wire [15:0] led_data_2 = {i_rdata_2[7:0], i_rdata_2[15:8]};

    always @(posedge i_clk) begin
        o_led_lat <= 0;
        o_led_red1 <= 0;
        o_led_red2 <= 0;
        o_led_clk <= 0;

        if(o_led_oe == 0) begin
            delay <= delay - 1;
            if(delay == 0) begin
                o_led_oe <= 1;
            end
        end

        case(state)
            STATE_READY:
            begin
                state <= STATE_PRE_DELAY;
                led <= 0;
                pwm_bit <= 0;
            end
            STATE_PRE_DELAY:
            begin
                state <= STATE_WRITING;
            end
            STATE_WRITING:  // Write one frame of LED data
            begin
                o_led_red1 <= pwm_lut_1[led_data_1[7:0]][pwm_bit];
                o_led_red2 <= pwm_lut_2[led_data_2[7:0]][pwm_bit];

                case(o_led_clk)
                    0:
                    begin
                        o_led_clk <= 1;
                        led <= led + 1;
                    end
                    1:
                    begin
                        o_led_clk <= 0;
                    end
                endcase

                if((led == 256) && (o_led_clk == 1)) begin
                    state <= STATE_WAIT_OE;
                    led <= 0;
                end
            end
            STATE_WAIT_OE:
            begin
                if(o_led_oe == 1) begin
                    state <= STATE_LATCH;
                end
            end
            STATE_LATCH:
            begin
                led <= led + 1;
                o_led_lat <= 1;

                if(led == 10) begin
                    state <= STATE_DELAY;

                    delay <= 1<<(pwm_bit);
                    o_led_oe <= 0;
                end
            end
            STATE_DELAY:
            begin
                state <= STATE_PRE_DELAY;
                led <= 0;
                pwm_bit <= pwm_bit + 1;

                if(pwm_bit == 4'd15) begin
                    state <= STATE_READY;
                end
            end
            default:
            begin
                state <= STATE_READY;
            end
        endcase
    end

endmodule
