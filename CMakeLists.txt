cmake_minimum_required(VERSION 3.15)
project(UselessConfigC C)

file(GLOB_RECURSE LIB_SOURCES "src/UselessConfigC/*.c")
file(GLOB_RECURSE LIB_HEADERS "include/usec/*.h")

add_library(usec STATIC ${LIB_SOURCES} ${LIB_HEADERS})
target_include_directories(usec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(test test/test.c)
target_link_libraries(test PRIVATE usec)