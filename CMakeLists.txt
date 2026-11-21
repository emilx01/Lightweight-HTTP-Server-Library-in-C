cmake_minimum_required(VERSION 3.10)

project(server_lib C)

add_library(server src/server_lib.c)
target_include_directories(server PUBLIC include)

add_executable(server_app examples/main.c)
