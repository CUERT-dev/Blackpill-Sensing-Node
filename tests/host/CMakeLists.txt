cmake_minimum_required(VERSION 3.28)
project(uart_sender_host_tests LANGUAGES C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(
    ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE
)

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

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_uart_sender PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(
    NAME test_uart_sender
    COMMAND test_uart_sender
)

add_executable(test_app_adc
    test_app_adc.c
    "${ROOT}/Core/Src/app_adc.c"
    "${ROOT}/lib/joystick/src/joystick.c"
)
target_include_directories(test_app_adc PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/fakes"
    "${ROOT}/Core/Inc"
    "${ROOT}/Contracts"
    "${ROOT}/lib/joystick/include"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_app_adc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_app_adc COMMAND test_app_adc)

add_executable(test_app_adc_selftest
    test_app_adc_selftest.c
    "${ROOT}/Core/Src/app_adc_selftest.c"
)
target_include_directories(test_app_adc_selftest PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/fakes" "${ROOT}/Core/Inc"
)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_app_adc_selftest PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME test_app_adc_selftest COMMAND test_app_adc_selftest)
