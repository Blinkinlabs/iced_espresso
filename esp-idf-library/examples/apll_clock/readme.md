# APLL clock example

This example shows how to use the ESP32's APLL clock as a source for the ICE40 FPGA. Some advantages of using the APLL to clock the FPGA are:
* The APLL is driven by a 40MHz crystal oscillator, that should be more stable than the internal ICE40 oscillator over temperature.
* The APLL has very fine graned settings, allowing for very accurate frequencies to be generated.
* The frequency of the APLL can be changed easily and on-the-fly using software on the ESP32, saving valuable gates on the ICE40.

On the ESP, a function 'start_clock' is provided, which configures the APLL to output the given clock frequency on the clock output line.

On the FPGA, the example is configured to use the clock line (which is connected to a global buffer on the FPGA) as the input clock for the device logic.

Note: When using an external clock input on the FPGA, be sure that the generated logic meets the timing requirements for the externally applied clock.
