cmake_minimum_required(VERSION 3.12)

set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB COMMON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/common/*.c*")

add_executable(client-with-usb-host "${CMAKE_CURRENT_SOURCE_DIR}/client_usb_host_main.cpp" "${COMMON_SRC}")
pico_add_extra_outputs(client-with-usb-host)
pico_set_binary_type(client-with-usb-host copy_to_ram)
target_link_libraries(client-with-usb-host
  PRIVATE
    pico_multicore
    hal-MapleBus
    hal-System
    hal-Usb-Host
    clientLib
    pico_stdlib
)
target_compile_options(client-with-usb-host PRIVATE
  -Wall
  #-Werror
  -O3
)

target_include_directories(client-with-usb-host
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/client>"
    "${PROJECT_SOURCE_DIR}/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/common")

# USB keyboard to Dreamcast keyboard bridge, wired straight to a controller cable with no
# bus transceiver. Defaults target the Seeed XIAO RP2040, which only breaks out 11 GPIO:
# GP26-GP29, GP6, GP7, GP0-GP4. GP6/GP7 (pads D4/D5) is the only adjacent pair that leaves
# the UART free. On a full Pico use -DKBD_BUS_START_PIN=14 -DKBD_LED_ACTIVE_LOW=false
if(NOT DEFINED KBD_BUS_START_PIN)
  set(KBD_BUS_START_PIN 6)
endif()
# XIAO user LEDs are wired to 3V3 and lit by pulling low; a Pico's GP25 is active high
if(NOT DEFINED KBD_LED_ACTIVE_LOW)
  set(KBD_LED_ACTIVE_LOW true)
endif()

# Keyboard layout the peripheral declares to the console. Values are the enums in
# DreamcastKeyboard.hpp: Language Japan=1 America=2 England=3 ... Type Key101=3 Key104=5
# Key106=7. A japanese release may want -DKBD_LANGUAGE=1 -DKBD_TYPE=7
if(NOT DEFINED KBD_LANGUAGE)
  set(KBD_LANGUAGE 2)
endif()
if(NOT DEFINED KBD_TYPE)
  set(KBD_TYPE 5)
endif()

# Push the console's lock LED state back to the usb keyboard, so caps lock lights up
if(NOT DEFINED KBD_LED_WRITEBACK)
  set(KBD_LED_WRITEBACK 1)
endif()

# Two variants of the same source: the plain one is silent, the -dbg one prints to UART0
# (GP0 tx / GP1 rx, XIAO pads D6/D7) at 115200 8N1
foreach(KBD_TARGET client-with-usb-keyboard client-with-usb-keyboard-dbg client-with-usb-keyboard-oled)
  if(KBD_TARGET STREQUAL "client-with-usb-keyboard")
    set(KBD_DEBUG_VAL 0)
    set(KBD_OLED_VAL 0)
  elseif(KBD_TARGET STREQUAL "client-with-usb-keyboard-dbg")
    set(KBD_DEBUG_VAL 1)
    set(KBD_OLED_VAL 0)
  else()
    # oled build keeps the uart tracing too, since the panel is a summary not a log
    set(KBD_DEBUG_VAL 1)
    set(KBD_OLED_VAL 1)
  endif()

  add_executable(${KBD_TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/client_usb_keyboard_main.cpp" "${COMMON_SRC}")
  pico_add_extra_outputs(${KBD_TARGET})
  pico_set_binary_type(${KBD_TARGET} copy_to_ram)
  pico_enable_stdio_uart(${KBD_TARGET} ${KBD_DEBUG_VAL})
  # never over usb: that port is the keyboard's host port
  pico_enable_stdio_usb(${KBD_TARGET} 0)

  target_link_libraries(${KBD_TARGET}
    PRIVATE
      pico_multicore
      hal-MapleBus
      hal-System
      hal-Usb-Host
      hal-Display
      clientLib
      pico_stdlib
  )
  target_compile_definitions(${KBD_TARGET} PRIVATE
    # SDCKA on this pin, SDCKB on the next one up
    P1_BUS_START_PIN=${KBD_BUS_START_PIN}
    # no direction pin: gpio drives the bus directly
    P1_DIR_PIN=-1
    # blue user LED on the XIAO
    USB_LED_PIN=25
    LED_ACTIVE_LOW=${KBD_LED_ACTIVE_LOW}
    KBD_DEBUG=${KBD_DEBUG_VAL}
    KBD_OLED=${KBD_OLED_VAL}
    KBD_LANGUAGE=${KBD_LANGUAGE}
    KBD_TYPE=${KBD_TYPE}
    KBD_LED_WRITEBACK=${KBD_LED_WRITEBACK}
  )
  target_compile_options(${KBD_TARGET} PRIVATE
    -Wall
    -O3
  )
  target_include_directories(${KBD_TARGET}
    PRIVATE
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/client>"
      "${PROJECT_SOURCE_DIR}/inc"
      "${CMAKE_CURRENT_SOURCE_DIR}/common")
endforeach()
