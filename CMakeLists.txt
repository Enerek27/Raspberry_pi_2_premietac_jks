cmake_minimum_required(VERSION 3.10)

project(premietac_pi)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Werror=implicit-function-declaration)
add_subdirectory(library_uart)
add_subdirectory(lib_protokol)
add_subdirectory(lib_premietac)
add_subdirectory(src)
