module top (
    input GPIO_18,

    output P1_2,

    output RGB0,
    output RGB1,
    output RGB2

);

    //########### Use ESP clock as system clock ############################

    wire clk = GPIO_18;
    assign P1_2 = clk;

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

endmodule
