add_library(uart STATIC
    uart.c
)

target_include_directories(uart PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)