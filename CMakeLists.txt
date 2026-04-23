# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD waveshare_rp2040_zero CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(encoder-sniffer C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ---- u8g2 (display library) ---------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    u8g2_fetch
    GIT_REPOSITORY https://github.com/olikraus/u8g2.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  "csrc"   # csrc/ has no CMakeLists.txt → MakeAvailable skips add_subdirectory
)
FetchContent_MakeAvailable(u8g2_fetch)

file(GLOB U8G2_SOURCES "${u8g2_fetch_SOURCE_DIR}/csrc/*.c")
add_library(u8g2lib STATIC ${U8G2_SOURCES})
target_include_directories(u8g2lib PUBLIC "${u8g2_fetch_SOURCE_DIR}/csrc")
target_compile_options(u8g2lib PRIVATE -w -ffunction-sections -fdata-sections)

# ---- WS2812 PIO ---------------------------------------------------------------
add_library(ws2812_pio INTERFACE)
pico_generate_pio_header(ws2812_pio ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

# ---- Main executable ----------------------------------------------------------
add_executable(encoder-sniffer main.c)

pico_set_program_name(encoder-sniffer "encoder-sniffer")
pico_set_program_version(encoder-sniffer "0.1")

target_include_directories(encoder-sniffer PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(encoder-sniffer
    pico_stdlib
    hardware_i2c
    hardware_gpio
    hardware_irq
    hardware_pio
    hardware_timer
    hardware_clocks
    u8g2lib
    ws2812_pio
)

pico_enable_stdio_uart(encoder-sniffer 0)
pico_enable_stdio_usb(encoder-sniffer 1)

pico_add_extra_outputs(encoder-sniffer)
