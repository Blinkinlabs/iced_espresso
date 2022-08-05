module top (
    input FSPI_CLK,
    input FSPI_MOSI,
    input FSPI_CS,
    output FSPI_MISO,

    input GPIO_18,

    output reg POWER_EN,
    output reg DATA,
    output reg ADDRESS,

//    output wire IO_23,

    output RGB0,
    output RGB1,
    output RGB2
);


    localparam ADDRESS_BUS_WIDTH = 16;      // Address bus width (16-bit words)
    localparam DATA_BUS_WIDTH = 16;         // Data bus width


    initial begin
        POWER_EN = 1;
    end

    //############ Clock / Reset ############################################

    wire clk;
    wire rst;

//    // Configure the HFOSC
//	SB_HFOSC #(
//        .CLKHF_DIV("0b01") // 00: 48MHz, 01: 24MHz, 10: 12MHz, 11: 6MHz
//    ) u_hfosc (
//       	.CLKHFPU(1'b1),
//       	.CLKHFEN(1'b1),
//        .CLKHF(clk)
//    );

    assign clk = GPIO_18;
    assign rst = 0; // TODO: Hardware reset input (?)

    //########### RGB LED ##################################################
    
    reg [15:0] led_duty;
    reg [15:0] red_duty;
    reg [15:0] green_duty;
    reg [15:0] blue_duty;

    initial begin
        led_duty = 16'd0;
        red_duty = 16'd65535;
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

//    //############ WS2822 programmer ########################################
//
//    wire [15:0] ws2822_address = spi_read_data;
//    reg ws2822_program_strobe;
//    wire ws2822_power_en;
//
//    reg [7:0] color_red;
//    reg [7:0] color_green;
//    reg [7:0] color_blue;
//
//    reg ws2822_data_strobe;        // strobe to start a dmx data output
//    reg ws2822_send_status;        // status to show if a dmx data output is in progress
//
//    initial begin
//        color_red = 0;
//        color_blue = 0;
//        color_green = 0;
//        ws2822_data_strobe = 0;
//        ws2822_send_status = 0;
//    end
//
//    ws2822_programmer prog(
//        .clk(clk),
//
//        .address(ws2822_address),
//        .program_strobe(ws2822_program_strobe),
//
//        .red(color_red),
//        .green(color_green),
//        .blue(color_blue),
//        .data_strobe(ws2822_data_strobe),
//
//        .power_en(ws2822_power_en),
//        .data_pin(DATA),
//        .address_pin(ADDRESS),
//        .data_strobe(ws2822_data_strobe)
//    );




    //############ Memory bus inputs ########################################

    wire [7:0] spi_command;
    wire [(ADDRESS_BUS_WIDTH-1):0] spi_address;

    wire spi_transaction_strobe;
    wire [(DATA_BUS_WIDTH-1):0] spi_write_data;
    reg [(DATA_BUS_WIDTH-1):0] spi_read_data;

    // Decode the SPI commands
    wire spi_mem_read_strobe = (spi_command[1:0] == 2'b00) && (spi_transaction_strobe);
    wire spi_mem_write_strobe = (spi_command[1:0] == 2'b01) && (spi_transaction_strobe);

    wire spi_reg_read_strobe = (spi_command[1:0] == 2'b10) && (spi_transaction_strobe);
    wire spi_reg_write_strobe = (spi_command[1:0] == 2'b11) && (spi_transaction_strobe);


    //############ DMX output ###############################################
    reg data_mode;                  // switch between outputting dmx on DATA or ADDRESS line
    reg address_mode;                  // switch between outputting dmx on DATA or ADDRESS line
    reg [9:0] dmx_channel_count;    // Number of DMX channels to output
    reg dmx_start_strobe;
    wire dmx_busy;

    initial begin
        data_mode = 0;
        address_mode = 0;
        dmx_channel_count = 0;
        dmx_start_strobe = 0;
    end

    wire dmx_out;                   // DMX output

    dmx_out dmx_out_1(
        .i_clk(clk),
        .i_rst(rst),

        .i_write_addr(spi_address[7:0]),
        .i_write_data(spi_write_data),
        .i_write_strobe(spi_mem_write_strobe),

        .i_channel_count(dmx_channel_count),
        .i_start_strobe(dmx_start_strobe),
        .o_busy(dmx_busy),
        .o_data(dmx_out)
    );

    //assign IO_23 = spi_write_data[0];

    //############ Configuration Registers ##################################

    always @(posedge clk) begin
        // Register Map
        //
        // 0x00F0: Red LED duty (0-65535)
        // 0x00F1: Green LED duty (0-65535)
        // 0x00F2: Blue LED duty (0-65535)
        dmx_start_strobe <= 0;

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

            8'h00:
            begin
                if(spi_reg_write_strobe)
                    POWER_EN <= spi_write_data[0];
    
                if(spi_reg_read_strobe)
                    spi_read_data <= {15'b0, POWER_EN};
            end
            8'h01:
            begin
                if(spi_reg_write_strobe)
                    dmx_channel_count <= spi_write_data[9:0];
    
                if(spi_reg_read_strobe)
                    spi_read_data <= {6'b0, dmx_channel_count};
            end
            8'h02:
            begin
                if(spi_reg_write_strobe)
                    {address_mode, data_mode} <= spi_write_data[1:0];
    
                if(spi_reg_read_strobe)
                    spi_read_data <= {14'b0, address_mode, data_mode};
            end
            8'h03:
            begin
                if(spi_reg_write_strobe)
                    dmx_start_strobe <= spi_write_data[0];
    
                if(spi_reg_read_strobe)
                    spi_read_data <= {15'b0, dmx_busy};
            end
        endcase

        // Update the DATA and ADDRESS outputs based on their enalble setting
        if (data_mode) begin
            DATA <= dmx_out;
        end
        else begin
            DATA <= 0;
        end

        if (address_mode) begin
            ADDRESS <= dmx_out;
        end
        else begin
            ADDRESS <= 0;
        end
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
