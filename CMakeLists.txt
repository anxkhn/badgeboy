cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (PICO_SDK_PATH must be set, or use build.sh).
include(pico_sdk_import.cmake)

project(badgeboy C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Target chip / board
#
# The badge uses an RP2350B (48-GPIO) part. The display data bus is on
# GPIO 32-39, which do not exist on the RP2350A (pico2). Build against an
# RP2350B board so those pins are valid; all pins are set explicitly in
# src/config.h, so the board's own defaults are irrelevant.
# ---------------------------------------------------------------------------
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD pimoroni_pico_plus2_rp2350)

pico_sdk_init()

# ---------------------------------------------------------------------------
# ROM selection (build time, never committed)
#
# Supply your own legally obtained .gb/.gbc ROM:
#   cmake -B build -DGBC_ROM=/abs/path/game.gbc ...
# The ROM is converted to a C array and embedded in flash. See
# docs/CONFIGURATION.md.
# ---------------------------------------------------------------------------
set(GBC_ROM "" CACHE STRING "Absolute path to the .gb/.gbc ROM to embed")
if(NOT GBC_ROM)
  message(FATAL_ERROR
    "No ROM specified. Pass -DGBC_ROM=/abs/path/game.gbc (see docs/CONFIGURATION.md).")
endif()
if(NOT EXISTS "${GBC_ROM}")
  message(FATAL_ERROR "GBC_ROM does not exist: ${GBC_ROM}")
endif()

set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
set(GEN_ROM ${GEN_DIR}/gb_rom.h)
add_custom_command(
  OUTPUT  ${GEN_ROM}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
  COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/rom2c.py "${GBC_ROM}" "${GEN_ROM}"
  DEPENDS "${GBC_ROM}" ${CMAKE_CURRENT_LIST_DIR}/tools/rom2c.py
  COMMENT "Embedding ROM ${GBC_ROM}"
  VERBATIM)
add_custom_target(generate_rom DEPENDS ${GEN_ROM})

# ---------------------------------------------------------------------------
# Display mode: FULLSCREEN (default) | ASPECT | CENTERED
# ---------------------------------------------------------------------------
set(GBC_DISPLAY_MODE "FULLSCREEN" CACHE STRING "FULLSCREEN | ASPECT | CENTERED")
set_property(CACHE GBC_DISPLAY_MODE PROPERTY STRINGS FULLSCREEN ASPECT CENTERED)
if(GBC_DISPLAY_MODE STREQUAL "CENTERED")
  set(DM 2)
elseif(GBC_DISPLAY_MODE STREQUAL "ASPECT")
  set(DM 1)
elseif(GBC_DISPLAY_MODE STREQUAL "FULLSCREEN")
  set(DM 0)
else()
  message(FATAL_ERROR "GBC_DISPLAY_MODE must be FULLSCREEN, ASPECT or CENTERED")
endif()

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_executable(badgeboy
  src/main.c
  src/tufty_lcd.c
)
add_dependencies(badgeboy generate_rom)

pico_generate_pio_header(badgeboy ${CMAKE_CURRENT_LIST_DIR}/src/st7789_parallel.pio)

target_compile_definitions(badgeboy PRIVATE DISPLAY_MODE=${DM})

target_include_directories(badgeboy PRIVATE
  src
  third_party/peanut-gb
  ${GEN_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(badgeboy
  pico_stdlib
  hardware_pio
  hardware_dma
  hardware_pwm
  hardware_gpio
)

pico_enable_stdio_usb(badgeboy 1)
pico_enable_stdio_uart(badgeboy 0)

pico_add_extra_outputs(badgeboy)   # produces build/badgeboy.uf2
