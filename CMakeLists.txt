cmake_minimum_required(VERSION 3.27)

project(premietac_pi
    VERSION 1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(raylib REQUIRED)

add_subdirectory(wifi)
add_subdirectory(lib_protokol)
add_subdirectory(lib_premietac)




add_executable(premietac_pi
    main.c

)

target_link_libraries(premietac_pi PRIVATE 
    raylib
    wifi
    protokol
    premietac
)

