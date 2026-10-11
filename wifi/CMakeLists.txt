add_library(wifi STATIC
    wifi.c
)

target_include_directories(wifi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)