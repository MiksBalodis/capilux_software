#!/bin/sh
# Host simulation of the flight firmware: builds src/main.cpp unmodified against sim/mock and
# runs the flight scenarios in sim.cpp. Needs g++ on Linux x86-64.
set -e
cd "$(dirname "$0")"
F="-std=gnu++17 -O1 -g -Imock -I../src -fno-gnu-unique -w"
# -fpermissive: main.cpp casts &__flash_binary_end to uint32_t (32-bit on the Pico)
SO="-fPIC -fpermissive -shared -Wl,--defsym,__flash_binary_end=0x10020000"
g++ $F $SO -o fw.so ../src/main.cpp
g++ $F -rdynamic -o sim sim.cpp -ldl
./sim
