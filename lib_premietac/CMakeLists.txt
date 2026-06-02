add_library(premietac STATIC
    premietac.c
)

target_include_directories(premietac PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(premietac PUBLIC
    protokol
    uart
    raylib
    pthread
    m
)