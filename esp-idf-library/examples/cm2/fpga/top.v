module top (
    input FSPI_CLK,
    input FSPI_MOSI,
    input FSPI_CS,
    output FSPI_MISO,

    output RGB0,
    output RGB1,
    output RGB2,

    output O1_RED_1,
    output O1_GREEN_1,
    output O1_BLUE_1,
    output O1_RED_2,
    output O1_GREEN_2,
    output O1_BLUE_2,

    output O2_RED_1,
    output O2_GREEN_1,
    output O2_BLUE_1,
    output O2_RED_2,
    output O2_GREEN_2,
    output O2_BLUE_2,

    output G_CLK,
    output G_LAT,
    output G_OE,

    input SW_1,
    input SW_2,
    input SW_T,

    output GPIO_7,
    output GPIO_8,
    output GPIO_18
);


    localparam ADDRESS_BUS_WIDTH = 16;      // Address bus width (16-bit words)
    localparam DATA_BUS_WIDTH = 16;         // Data bus width

    //############ Unused outputs ###########################################

    assign O1_GREEN_1 = 1'b0;
    assign O1_BLUE_1 = 1'b0;
    assign O1_RED_2 = 1'b0;
    assign O1_GREEN_2 = 1'b0;
    assign O1_BLUE_2 = 1'b0;

    assign O2_GREEN_1 = 1'b0;
    assign O2_BLUE_1 = 1'b0;
    assign O2_RED_2 = 1'b0;
    assign O2_GREEN_2 = 1'b0;
    assign O2_BLUE_2 = 1'b0;

    //############ Mirror switch pins to ESP32 ##############################

    assign GPIO_7 = SW_1;
    assign GPIO_8 = SW_2;
    assign GPIO_18 = SW_T;

    //############ Clock / Reset ############################################

    wire clk;
    wire rst;


    // Configure the HFOSC
    /* verilator lint_off PINMISSING */
	SB_HFOSC #(
        .CLKHF_DIV("0b01") // 00: 48MHz, 01: 24MHz, 10: 12MHz, 11: 6MHz
    ) u_hfosc (
       	.CLKHFPU(1'b1),
       	.CLKHFEN(1'b1),
        .CLKHF(clk)
    );
    /* verilator lint_on PINMISSING */

    assign rst = 0; // TODO: Hardware reset input (?)

    //########### ICND2026 driver #1 ###########################################

    wire [7:0] matrix_1_raddr;
    wire [15:0] matrix_1_rdata;

    wire [7:0] matrix_1_waddr;
    wire [15:0] matrix_1_wdata;
    reg matrix_1_we;

    wire [7:0] matrix_2_raddr;
    wire [15:0] matrix_2_rdata;

    wire [7:0] matrix_2_waddr;
    wire [15:0] matrix_2_wdata;
    reg matrix_2_we;

    SB_RAM40_4K matrix_1_memory (
        .RDATA(matrix_1_rdata),
        .RADDR({3'd0, matrix_1_raddr}),
        .RCLK(clk),
        .RCLKE(1'b1),
        .RE(1'b1),
        
        .WADDR({3'd0, matrix_1_waddr}),
        .WCLK(clk),
        .WCLKE(1'b1),
        .WDATA(matrix_1_wdata),
        .WE(matrix_1_we),
        .MASK(16'd0)
    );

    SB_RAM40_4K matrix_2_memory (
        .RDATA(matrix_2_rdata),
        .RADDR({3'd0, matrix_2_raddr}),
        .RCLK(clk),
        .RCLKE(1'b1),
        .RE(1'b1),
        
        .WADDR({3'd0, matrix_2_waddr}),
        .WCLK(clk),
        .WCLKE(1'b1),
        .WDATA(matrix_2_wdata),
        .WE(matrix_2_we),
        .MASK(16'd0)
    );

    matrix matrix_1 (
        .i_clk(clk),

        .o_led_oe(G_OE),
        .o_led_clk(G_CLK),
        .o_led_lat(G_LAT),
        .o_led_red1(O1_RED_1),
        .o_led_red2(O2_RED_1),

        .o_raddr_1(matrix_1_raddr),
        .i_rdata_1(matrix_1_rdata),

        .o_raddr_2(matrix_2_raddr),
        .i_rdata_2(matrix_2_rdata)
    );

    //########### Status LEDS ##############################################
    
    reg [15:0] led_duty;
    reg [15:0] red_duty;
    reg [15:0] green_duty;
    reg [15:0] blue_duty;

    initial begin
        led_duty = 16'd0;
        red_duty = 16'd0;
        green_duty = 16'd30000;
        blue_duty = 16'd0;
    end

    wire LED_RED = (led_duty < red_duty);
    wire LED_GREEN = (led_duty < green_duty);
    wire LED_BLUE = (led_duty < blue_duty);

    always @(posedge clk) begin
        led_duty <= led_duty + 1;
    end

    SB_RGBA_DRV #(
        .CURRENT_MODE("0b1"),       // half-current mode
        .RGB0_CURRENT("0b000001"),  // 2 mA
        .RGB1_CURRENT("0b000001"),  // 2 mA
        .RGB2_CURRENT("0b000001")   // 2 mA
    ) RGBA_DRV (
        .RGB0(RGB0),
        .RGB1(RGB1),
        .RGB2(RGB2),
        .RGBLEDEN(1'b1),
        .RGB0PWM(LED_RED),
        .RGB1PWM(LED_GREEN),
        .RGB2PWM(LED_BLUE),
        .CURREN(1'b1)
    );


    //############ Memory bus inputs ########################################

    wire [7:0] spi_command;
    wire [(ADDRESS_BUS_WIDTH-1):0] spi_address;

    wire spi_transaction_strobe;
    wire [(DATA_BUS_WIDTH-1):0] spi_write_data;
    reg [(DATA_BUS_WIDTH-1):0] spi_read_data;

    // Decode the SPI commands
//    wire spi_mem_read_strobe = (spi_command[1:0] == 2'b00) && (spi_transaction_strobe);
    wire spi_mem_write_strobe = (spi_command[1:0] == 2'b01) && (spi_transaction_strobe);

    wire spi_reg_read_strobe = (spi_command[1:0] == 2'b10) && (spi_transaction_strobe);
    wire spi_reg_write_strobe = (spi_command[1:0] == 2'b11) && (spi_transaction_strobe);


    assign matrix_1_waddr = spi_address[7:0];
    assign matrix_1_wdata = spi_write_data;

    assign matrix_2_waddr = spi_address[7:0];
    assign matrix_2_wdata = spi_write_data;

    //############ Configuration Registers ##################################



    always @(posedge clk) begin
        // Register Map
        //
        // 0x00F0: Red LED duty (0-65535)
        // 0x00F1: Green LED duty (0-65535)
        // 0x00F2: Blue LED duty (0-65535)

        case(spi_address[7:0])
            8'hF0:
            begin
                if(spi_reg_write_strobe)
                    red_duty <= spi_write_data;
    
                if(spi_reg_read_strobe)
                    spi_read_data <= red_duty;
            end
            8'hF1:
            begin
                if(spi_reg_write_strobe)
                    green_duty <= spi_write_data;
    
                if(spi_reg_read_strobe)
                    spi_read_data <= green_duty;
            end
            8'hF2:
            begin
                if(spi_reg_write_strobe)
                    blue_duty <= spi_write_data;
    
                if(spi_reg_read_strobe)
                    spi_read_data <= blue_duty;
            end
            default:
            ;
        endcase

        // Ram Map
        //
        // 0x0000 - 0x00FF: LED output 1 RAM
        // 0x0100 - 0x01FF: LED output 2 RAM
        matrix_1_we <= 0;
        matrix_2_we <= 0;

        case(spi_address[15:8])
            8'h00:
            begin
                if(spi_mem_write_strobe) begin
                    matrix_1_we <= 1;
                end
            end
            8'h01:
            begin
                if(spi_mem_write_strobe) begin
                    matrix_2_we <= 1;
                end
            end
            default:
                ;
        endcase
    end


    //############ SPI Input ################################################
    
    spi spi_1(
        .i_clk(clk),
        .i_rst(rst),

        .i_cs(FSPI_CS),
        .i_sck(FSPI_CLK),
        .i_mosi(FSPI_MOSI),
        .o_miso(FSPI_MISO),

        .o_address(spi_address),
        .o_command(spi_command),

        .o_transaction_strobe(spi_transaction_strobe),
        .o_write_data(spi_write_data),
        .i_read_data(spi_read_data)
    );


endmodule
