cmake_minimum_required(VERSION 2.8)

add_definitions(-Wall)

project(esprom)

INCLUDE(CPack)

include_directories ("${PROJECT_SOURCE_DIR}")

FILE(GLOB c_source_files *.c)

add_library(esprom SHARED ${c_source_files} )

install (TARGETS esprom DESTINATION lib)
install (FILES libesprom.h DESTINATION include)



