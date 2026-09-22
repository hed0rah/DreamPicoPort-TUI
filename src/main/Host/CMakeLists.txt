cmake_minimum_required(VERSION 3.12)

set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB HOST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/host*.c*")

if ("${PICO_BOARD}" STREQUAL "pico2")
  # Increase the CPU frequency to 150MHz for the pico2 board
  add_definitions(-DCPU_FREQ_KHZ=150000)
endif()



#
# host
#

add_executable(host ${HOST_SRC})
if ("${PICO_BOARD}" STREQUAL "pico2")
  # Doing this on the pico would only leave about 10 KB of headroom in RAM
  # pico2 has twice the RAM, so this is feasible there and helps with execution speed
  pico_set_binary_type(host copy_to_ram)
endif()
pico_add_extra_outputs(host)
target_link_libraries(host
  PRIVATE
    pico_multicore
    hardware_exception
    hal-MapleBus
    hal-System
    hal-Usb-Client
    pico_stdio_usb
    hostLib
)
target_compile_options(host PRIVATE
  -Wall
  -Werror
  -O3
)

target_include_directories(host
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/host>"
    "${PROJECT_SOURCE_DIR}/inc")


#
# bench-host: stands in for a Dreamcast when testing a client build on the bench.
# Same firmware as host, but with the transceiver direction pin disabled so it can be
# wired straight to another rp2040 with two signal wires and a ground.
#
add_executable(bench-host ${HOST_SRC})
pico_add_extra_outputs(bench-host)
target_link_libraries(bench-host
  PRIVATE
    pico_multicore
    hardware_exception
    hal-MapleBus
    hal-System
    hal-Usb-Client
    pico_stdio_usb
    hostLib
)
# These only reach files compiled into this target. DppSettings lives in hal-System, a
# static library built once for every target, so its compiled-in defaults still come from
# configuration.h. At runtime the bus is configured from DppSettings, not from these, so a
# freshly flashed board still comes up with the stock direction pin. Set it over the serial
# console instead:  =gpiodir0 -1  then  =save
target_compile_definitions(bench-host PRIVATE
  # sdcka on gp10, sdckb on gp11
  P1_BUS_START_PIN=10
  # no transceiver: gpio drives the bus directly
  P1_DIR_PIN=-1
)
target_compile_options(bench-host PRIVATE
  -Wall
  -Werror
  -O3
)
target_include_directories(bench-host
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/host>"
    "${PROJECT_SOURCE_DIR}/inc")


#
# zero_host
#
add_executable(zero_host ${HOST_SRC})
if ("${PICO_BOARD}" STREQUAL "pico2")
  # Doing this on the pico would only leave about 10 KB of headroom in RAM
  # pico2 has twice the RAM, so this is feasible there and helps with execution speed
  pico_set_binary_type(zero_host copy_to_ram)
endif()
pico_add_extra_outputs(zero_host)
target_link_libraries(zero_host
  PRIVATE
    pico_multicore
    hardware_exception
    hal-MapleBus
    hal-System
    hal-Usb-Client
    pico_stdio_usb
    hostLib
)
target_compile_options(zero_host PRIVATE
  -Wall
  -Werror
  -O3
)
target_compile_definitions(zero_host
  PUBLIC
    SELECTED_NUMBER_OF_DEVICES=4
    P3_BUS_START_PIN=2
    P4_BUS_START_PIN=4
    )

target_include_directories(zero_host
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/zero_host>"
    "${PROJECT_SOURCE_DIR}/inc")
