module top (
    input FSPI_CLK,
    input FSPI_MOSI,
    input FSPI_CS,
    output FSPI_MISO,

    output RGB0,
    output RGB1,
    output RGB2
);

    localparam ADDRESS_BUS_WIDTH = 16;      // Address bus width (16-bit words)
    localparam DATA_BUS_WIDTH = 16;         // Data bus width
    localparam OUTPUT_COUNT = 4;           // Total number of outputs
    localparam GENVAR_COUNT = 4;           // Outputs to generate with the genvar loop

    //############ Clock / Reset ############################################

    wire clk;
    wire rst;

    // Configure the HFOSC
	SB_HFOSC #(
        .CLKHF_DIV("0b00") // 00: 48MHz, 01: 24MHz, 10: 12MHz, 11: 6MHz
    ) u_hfosc (
       	.CLKHFPU(1'b1),
       	.CLKHFEN(1'b1),
        .CLKHF(clk)
    );

    assign rst = 0; // TODO: Hardware reset input (?)


    //########### Status LEDS ##############################################
    
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


    //############ Memory bus inputs ########################################

    wire [7:0] spi_command;
    wire [(ADDRESS_BUS_WIDTH-1):0] spi_address;

    wire spi_transaction_strobe;
    wire [(DATA_BUS_WIDTH-1):0] spi_write_data;
    reg [(DATA_BUS_WIDTH-1):0] spi_read_data;

    // Decode the SPI commands
    //wire spi_mem_read_strobe = (spi_command[1:0] == 2'b00) && (spi_transaction_strobe);
    wire spi_mem_write_strobe = (spi_command[1:0] == 2'b01) && (spi_transaction_strobe);

    wire spi_reg_read_strobe = (spi_command[1:0] == 2'b10) && (spi_transaction_strobe);
    wire spi_reg_write_strobe = (spi_command[1:0] == 2'b11) && (spi_transaction_strobe);


    //############ Configuration Registers ##################################


    // Register Map
    //
    // 0x00F0: Red LED duty (0-65535)
    // 0x00F1: Green LED duty (0-65535)
    // 0x00F2: Blue LED duty (0-65535)

    wire [3:0] reg_offset = spi_address[3:0];

    // Map the configuration registers into memory
    always @(posedge clk) begin
        if(spi_address[7:0] == 8'hF0) begin
            if(spi_reg_write_strobe)
                red_duty <= spi_write_data;

            if(spi_reg_read_strobe)
                spi_read_data <= red_duty;
        end

        if(spi_address[7:0] == 8'hF1) begin
            if(spi_reg_write_strobe)
                green_duty <= spi_write_data;

            if(spi_reg_read_strobe)
                spi_read_data <= green_duty;
        end

        if(spi_address[7:0] == 8'hF2) begin
            if(spi_reg_write_strobe)
                blue_duty <= spi_write_data;

            if(spi_reg_read_strobe)
                spi_read_data <= blue_duty;
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
