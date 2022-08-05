set(embed
    "../../fpga/top.bin")

idf_component_register(
    SRCS "fpga_bin.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "${embed}")
