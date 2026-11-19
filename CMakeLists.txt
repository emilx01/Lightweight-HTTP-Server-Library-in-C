cmake_minimum_required(VERSION 3.10)

project(server_lib C)

add_library(server server_lib.c server_lib.h)

add_executable(client client.c)

target_link_libraries(client PRIVATE server)

