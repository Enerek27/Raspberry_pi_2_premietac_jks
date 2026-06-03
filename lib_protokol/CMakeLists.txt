add_library(protokol STATIC
    protokol.c
)

target_include_directories(protokol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(protokol PUBLIC
    
    uart
    
)