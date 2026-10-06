cmake_minimum_required(VERSION 3.22)
project(pron_jni LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PRON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PRON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(../../../../../core ${CMAKE_CURRENT_BINARY_DIR}/core)

add_library(pron_jni SHARED pron_jni.cpp)
target_link_libraries(pron_jni PRIVATE pron_core log)
# 16 KB page size support (Android 15+)
target_link_options(pron_jni PRIVATE -Wl,-z,max-page-size=16384)
