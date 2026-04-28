# Flash helper — reusable CMake function.
#
# Usage in CMakeLists.txt:
#   include(cmake/Flash.cmake)
#   pico_add_flash_target(my-target PORT ${FLASH_PORT} UID ${FLASH_UID})
#
# Keyword arguments (all optional):
#   PORT  serial port override (e.g. COM3)
#   UID   RP2040 unique ID (hex) to target a specific device
#
# The function locates flash.py one level above this file (i.e. in the
# project root), so the layout must be:
#   <root>/flash.py
#   <root>/cmake/Flash.cmake

function(pico_add_flash_target target)
    cmake_parse_arguments(_F "" "PORT;UID" "" ${ARGN})

    find_package(Python3 QUIET)
    if(NOT Python3_FOUND)
        message(STATUS "Python3 not found — flash target disabled")
        return()
    endif()

    # CMAKE_CURRENT_FUNCTION_LIST_DIR is the directory of this file (cmake/).
    set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/flash.py")
    set(_uf2    "$<TARGET_FILE_DIR:${target}>/${target}.uf2")

    set(_args)
    if(_F_PORT)
        list(APPEND _args --port "${_F_PORT}")
    endif()
    if(_F_UID)
        list(APPEND _args --uid "${_F_UID}")
    endif()

    add_custom_target(flash ALL
        COMMAND ${Python3_EXECUTABLE} "${_script}" ${_args} "${_uf2}"
        COMMENT "Flashing ${target}.uf2 to device..."
        VERBATIM
    )
    add_dependencies(flash ${target})
endfunction()
