cmake_minimum_required(VERSION 3.16)

set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "")
if(POLICY CMP0111)
  cmake_policy(SET CMP0111 OLD)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ESP32-SwitchBot)
add_compile_options(-Wno-error=stringop-truncation -Wno-error=maybe-uninitialized -Wno-stringop-truncation)