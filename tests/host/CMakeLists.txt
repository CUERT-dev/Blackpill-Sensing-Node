cmake_minimum_required(VERSION 3.28)
project(blackpill_host_tests LANGUAGES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(
    ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE
)

# ----------------------------------------------------
# Target 1: Rodaina UART Sender Tests
# ----------------------------------------------------
add_executable(test_uart_sender
    test_uart_sender.c
    "${ROOT}/lib/gp-protocol/src/gp-protocol.c"
    "${ROOT}/lib/uart_sender/src/uart_sender.c"
)

target_include_directories(test_uart_sender PRIVATE
    "${ROOT}/Contracts"
    "${ROOT}/lib/gp-protocol/include"
    "${ROOT}/lib/uart_sender/include"
)

# ----------------------------------------------------
# Target 2: Boody Joystick ADC Tests
# ----------------------------------------------------
add_executable(test_joystick
    test_joystick.c
    "${ROOT}/lib/joystick/src/joystick.c"
)

target_include_directories(test_joystick PRIVATE
    "${ROOT}/Contracts"
    "${ROOT}/lib/joystick/include"
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_uart_sender PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
    target_compile_options(test_joystick PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(
    NAME test_uart_sender
    COMMAND test_uart_sender
)

add_test(
    NAME test_joystick
    COMMAND test_joystick
)