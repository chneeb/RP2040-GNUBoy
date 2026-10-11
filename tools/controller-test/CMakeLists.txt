# Standalone NES Mini / Wii Classic controller tests for the ResTouch
# (i2c0, SDA GP4, SCL GP5), output over USB serial. Not part of the emulator.
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(controller_test C CXX ASM)
pico_sdk_init()

foreach(tool i2cscan padtest)
  add_executable(${tool} ${tool}.c)
  target_link_libraries(${tool} pico_stdlib hardware_i2c)
  pico_enable_stdio_usb(${tool} 1)
  pico_enable_stdio_uart(${tool} 0)
  pico_add_extra_outputs(${tool})
endforeach()
