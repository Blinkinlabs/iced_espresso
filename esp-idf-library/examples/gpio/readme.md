# GPIO

This is a production test for the iced espresso boards.

On the FPGA, a simplified GPIO gateware is implemented, that allows the ESP32 to use the FPGA as a I/O expander.

On the ESP32, a test app is run that:
1. Loads the GPIO gateware into the FPGA
2. Tests that all of the GPIO pins between the ESP32 and ICE40 are connected
3. Outputs a pulse train across all of the output pins on the board, which can then be viewed using a logic analyzer to verify that all of the pins are connected to the headers.
