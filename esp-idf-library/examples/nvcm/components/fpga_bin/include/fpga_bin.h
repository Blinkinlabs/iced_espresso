#pragma once

#define FPGA_BIN_VERSION "test-2-ga2e1cdd-dirty"

//! @brief fpga_bin file table entry
typedef struct {
    const char *name;         //!< File name
    const uint8_t *start;     //!< Pointer to the start of the file in ROM
    const uint8_t *end;       //!< Pointer to the end of the file in ROM
} fpga_bin_entry_t;

#define FPGA_BIN_ENTRY_COUNT 1     //!< Number of entries in the fpga_bin table

//! @brief fpga_bin entry table 
extern const fpga_bin_entry_t fpga_bin_entries[FPGA_BIN_ENTRY_COUNT];
