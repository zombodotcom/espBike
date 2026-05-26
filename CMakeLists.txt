# Top-level ESP-IDF project file for ebike_ble_bridge.
# Build with ESP-IDF v6.0:
#   idf.py set-target esp32      # or esp32c3
#   idf.py build
#   idf.py -p <PORT> flash monitor
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ebike_ble_bridge)
