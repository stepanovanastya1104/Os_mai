cmake_minimum_required(VERSION 4.2)
project(os C)

set(CMAKE_C_STANDARD 11)

add_executable(os
        parent.c
        child.c)
