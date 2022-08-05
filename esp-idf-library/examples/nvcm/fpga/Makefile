TARGET = top

VERILOG_FILES = \
	top.v \
	sync_ss.v \
	spi.v \
	toggle_to_strobe.v

PIN_CONFIG_FILE = iced-espresso-revb.pcf
CLOCK_CONSTRAINTS_FILE = clocks.py

default: $(TARGET).bin

$(TARGET).json: $(VERILOG_FILES)
	yosys \
		-q \
		-p "synth_ice40 -abc2 -relut -top ${TARGET} -json $(TARGET).json" \
		-l $(TARGET)-yosys.log \
		$(VERILOG_FILES)

TARGET_MODULE = spi_1
view-ideal:
	yosys -p 'read_verilog ${VERILOG_FILES}; proc; opt; select ${TARGET_MODULE}; show -format dot -viewer xdot -pause' &

view-real:
	yosys -p 'read_verilog ${VERILOG_FILES}; proc; opt; synth_ice40; show -format dot -viewer xdot -pause' &

$(TARGET).asc: $(TARGET).json $(PIN_CONFIG_FILE) $(CLOCK_CONSTRAINTS_FILE)
	nextpnr-ice40 \
		--up5k \
		--package sg48 \
		--pre-pack $(CLOCK_CONSTRAINTS_FILE) \
		--json $(TARGET).json \
		--pcf $(PIN_CONFIG_FILE) \
		--asc $(TARGET).asc \
		-l $(TARGET)-nextpnr.log

$(TARGET).bin: $(TARGET).asc
	icepack $(TARGET).asc $(TARGET).bin

stats: $(TARGET).json $(TARGET).asc
	sed -n '/=== top ===/,/6\.28/p' $(TARGET)-yosys.log
	sed -n '/Info: Device utilisation/,/Info: Placed/p' $(TARGET)-nextpnr.log
	fgrep 'Info: Max frequency for clock' $(TARGET)-nextpnr.log	

lint: $(VERILOG_FILES)
	verilator --lint-only -Wall -Wno-DECLFILENAME -Ihw_blocks $(VERILOG_FILES)

.PHONY: clean
clean:
	$(RM) -f \
		$(TARGET).json \
		$(TARGET).asc \
		$(TARGET)-yosys.log \
		$(TARGET)-nextpnr.log \
		$(TARGET).bin
