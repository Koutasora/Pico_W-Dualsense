cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(dualsense_test C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

pico_sdk_init()

add_compile_definitions(
        GAMEPAD_CORE_EMBEDDED=1
        GAMEPAD_CORE_EXTERNAL_SO_DEFINES="gc_config.h"
        PICO_MALLOC_PANIC=0
)

add_subdirectory(lib/Gamepad-Core/Source)
add_executable(dualsense_test src/main.cpp src/web/dhcpserver.c)

# Panel page embedded into the firmware (served over Wi-Fi).
set(PANEL_HTML ${CMAKE_CURRENT_SOURCE_DIR}/panel/panel.html)
set(PANEL_HTML_H ${CMAKE_CURRENT_BINARY_DIR}/generated/panel_html.h)
add_custom_command(
        OUTPUT ${PANEL_HTML_H}
        COMMAND ${CMAKE_COMMAND} -DIN=${PANEL_HTML} -DOUT=${PANEL_HTML_H} -DNAME=panel_html
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
        DEPENDS ${PANEL_HTML} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
        COMMENT "Embedding panel.html"
)
add_custom_target(panel_html_gen DEPENDS ${PANEL_HTML_H})
add_dependencies(dualsense_test panel_html_gen)

target_include_directories(dualsense_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/web
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_include_directories(GamepadCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# add pico_stdlib to the GamepadCore target
target_link_libraries(GamepadCore PUBLIC pico_stdlib)

target_link_libraries(dualsense_test
        pico_stdlib
        pico_btstack_classic
        pico_cyw43_arch_lwip_threadsafe_background
        pico_btstack_cyw43
        hardware_gpio
        GamepadCore
)

# Configurações do Pico
pico_enable_stdio_usb(dualsense_test 1)
pico_enable_stdio_uart(dualsense_test 0)
pico_add_extra_outputs(dualsense_test)


add_custom_command(TARGET dualsense_test POST_BUILD
    COMMAND echo "Build complete: ${CMAKE_CURRENT_BINARY_DIR}/dualsense_test.elf"
    COMMAND picotool reboot -u -f || echo "Picotool: não foi possível reiniciar para BOOTSEL"
    COMMAND ${CMAKE_COMMAND} -E sleep 1.5
    COMMAND picotool load -x ${CMAKE_CURRENT_BINARY_DIR}/dualsense_test.elf -f
    COMMENT "Binário gerado. Tentando reiniciar em BOOTSEL e gravar automaticamente usando o ELF..."
)