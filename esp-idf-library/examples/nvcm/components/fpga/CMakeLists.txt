idf_component_register(
    SRCS
        "fpga_comms.c"
        "fpga_loader.c"
        "master_spi.c"
        "output_trans_pool.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        fpga_bin
    )
