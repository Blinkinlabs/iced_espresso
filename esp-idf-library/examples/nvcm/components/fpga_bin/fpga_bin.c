#include <stdint.h>
#include "fpga_bin.h"

extern const unsigned char top_bin_start asm("_binary_top_bin_start");
extern const unsigned char top_bin_end asm("_binary_top_bin_end");

const fpga_bin_entry_t fpga_bin_entries[FPGA_BIN_ENTRY_COUNT] = {
    {
        .name = "top.bin",
        .start = &top_bin_start,
        .end = &top_bin_end,
    },
};
