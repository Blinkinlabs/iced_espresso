# ICEd ESPresso ESP-IDF library

# Toolchain setup

## Setup instructions (Debian)

From a new Debian install (11.3):

    sudo apt install build-essential python3-pip cmake git git-gui tmux vim

Add the current user to dialout, to be able to program the board:

    sudo usermod -a -G dialout $USER

Log out and then back in to apply the group changes

Then install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/get-started/index.html):

    mkdir -p ~/esp
    cd ~/esp
    git clone -b v4.4 --recursive https://github.com/espressif/esp-idf.git
  
    cd ~/esp/esp-idf
    ./install.sh esp32s2

Create an ssh key for use with [github](https://docs.github.com/en/authentication/connecting-to-github-with-ssh/generating-a-new-ssh-key-and-adding-it-to-the-ssh-agent):

    ssh-keygen -t ed25519 -C "your_email@example.com"
    git config --global user.email "your_email@example.com"
    git config --global user.name "Your Name"

Then clone this repo:

    cd ~/
    git clone git@github.com:Blinkinlabs/iced_espresso.git

And patch the ESP-IDF to enable a SPI fast path:

    cd ~/esp/esp-idf
    git am ~/iced_espresso/examples/components/fpga/idf-patches/*


## Building

Set up the environment and build:

    . $HOME/esp/esp-idf/export.sh
    cd ~/iced_espresso/examples/wifi_blinky/
    idf.py build

If all went well, the board can be flashed:

    idf.py flash monitor

# Building the FPGA bitstream

First, build and install the icestorm toolchain:

    sudo apt-get install build-essential clang bison flex libreadline-dev \
                         gawk tcl-dev libffi-dev git mercurial graphviz   \
                         xdot pkg-config python python3 libftdi-dev \
                         python3-dev libboost-dev libeigen3-dev \
                         libboost-dev libboost-filesystem-dev libboost-thread-dev libboost-program-options-dev libboost-iostreams-dev

    git clone https://github.com/cliffordwolf/icestorm.git icestorm
    cd icestorm
    make -j$(nproc)
    sudo make install
    cd ..

    git clone https://github.com/YosysHQ/yosys.git
    cd yosys
    make -j$(nproc)
    sudo make install
    cd ..

    git clone https://github.com/YosysHQ/nextpnr nextpnr
    cd nextpnr
    cmake -DARCH=ice40 -DCMAKE_INSTALL_PREFIX=/usr/local .
    make -j$(nproc)
    sudo make install

Then, build the bitstream:

    cd ~/iced_espresso/examples/.../fpga/
    make


