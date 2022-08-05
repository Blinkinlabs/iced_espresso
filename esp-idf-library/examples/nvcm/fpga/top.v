module top (
    input FSPI_CLK,
    input FSPI_D,
    input FSPI_CS0,
    output FSPI_Q,

    input FSPI_WP,
    input FSPI_HD,

    input GPIO_7,
    input GPIO_8,
    input GPIO_18,
    input GPIO_21,
    input GPIO_33,
    input GPIO_34,
    input GPIO_35,
    input GPIO_38,

    output P1_2,
    output P1_3,
    output P1_4,
    output P1_5,
    output P1_6,
    output P1_7,
    output P1_8,
    output P1_9,

    output P2_4,
    output P2_5,
    output P2_6,
    output P2_7,
    output P2_8,
    output P2_9,
    output P2_10,
    output P2_11,
    output P2_12,
    output P2_13,
    output P2_14,
    output P2_15,
    output P2_16,
    output P2_17,

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
        green_duty = 16'd15535;
        blue_duty = 16'd15535;
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

    //############ GPIO ########################################

    reg [7:0] p1_pins;

    assign {
        P1_9,
        P1_8,
        P1_7,
        P1_6,
        P1_5,
        P1_4,
        P1_3,
        P1_2} = p1_pins;

    reg [13:0] p2_pins;

    assign {
        P2_17,
        P2_16,
        P2_15,
        P2_14,
        P2_13,
        P2_12,
        P2_11,
        P2_10,
        P2_9,
        P2_8,
        P2_7,
        P2_6,
        P2_5,
        P2_4
        } = p2_pins;

    wire [9:0] esp_inputs;

    assign esp_inputs = {
        GPIO_38,
        GPIO_35,
        GPIO_34,
        GPIO_33,
        GPIO_21,
        GPIO_18,
        GPIO_8,
        GPIO_7,

        FSPI_HD,
        FSPI_WP
        };
    
    //############ Memory bus inputs ########################################

    wire [7:0] spi_command;
    wire [(ADDRESS_BUS_WIDTH-1):0] spi_address;

    wire spi_transaction_strobe;
    wire [(DATA_BUS_WIDTH-1):0] spi_write_data;
    reg [(DATA_BUS_WIDTH-1):0] spi_read_data;

    initial begin
        spi_read_data <= 0;
    end

    // Decode the SPI commands
    //wire spi_mem_read_strobe = (spi_command[1:0] == 2'b00) && (spi_transaction_strobe);
    wire spi_mem_write_strobe = (spi_command[1:0] == 2'b01) && (spi_transaction_strobe);

    wire spi_reg_read_strobe = (spi_command[1:0] == 2'b10) && (spi_transaction_strobe);
    wire spi_reg_write_strobe = (spi_command[1:0] == 2'b11) && (spi_transaction_strobe);


    //############ Configuration Registers ##################################


    // Register Map
    // 
    // 0x0000: P1 output register
    // 0x0001: P2 output register
    // 0x0002: ESP input pins register

    // Map the configuration registers into memory
    always @(posedge clk) begin
        if(spi_address[15:0] == 16'h0000) begin
            if(spi_reg_write_strobe)
                p1_pins <= spi_write_data;
        end

        if(spi_address[15:0] == 16'h0001) begin
            if(spi_reg_write_strobe)
                p2_pins <= spi_write_data;
        end

        if(spi_address[15:0] == 16'h0002) begin
            if(spi_reg_read_strobe)
                spi_read_data <= {6'd0,esp_inputs};
        end

    end

    //############ SPI Input ################################################
    
    spi spi_1(
        .i_clk(clk),
        .i_rst(rst),

        .i_cs(FSPI_CS0),
        .i_sck(FSPI_CLK),
        .i_mosi(FSPI_D),
        .o_miso(FSPI_Q),

        .o_address(spi_address),
        .o_command(spi_command),

        .o_transaction_strobe(spi_transaction_strobe),
        .o_write_data(spi_write_data),
        .i_read_data(spi_read_data)
    );


endmodule
