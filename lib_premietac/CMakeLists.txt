add_library(premietac STATIC
    premietac.c
)

target_include_directories(premietac PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(premietac PUBLIC
    protokol
)

target_link_libraries(premietac PUBLIC
    uart
)