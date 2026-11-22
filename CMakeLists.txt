cmake_minimum_required(VERSION 3.16)

# Die Quellen sind C++17-kompatibel; ESP-IDF wählt selbst den Standard
# (v5.x: gnu++17, v6.x: gnu++26).

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(network_cable_tester)
