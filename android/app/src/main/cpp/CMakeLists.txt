cmake_minimum_required(VERSION 3.22)
project(pron_jni LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PRON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PRON_BUILD_SHARED ON CACHE BOOL "" FORCE)
# engine/ builds pron_core, pron_engine (shared, exports the whole pron_c.h API) and downloads the
# onnxruntime / sherpa-onnx android prebuilts for ANDROID_ABI.
add_subdirectory(../../../../../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)

add_library(pron_jni SHARED pron_jni.cpp)
target_link_libraries(pron_jni PRIVATE pron_engine log)
# 16 KB page size support (Android 15+)
target_link_options(pron_jni PRIVATE -Wl,-z,max-page-size=16384)

# Safety net: make sure prebuilt dependency .so files fetched by engine/ get packaged into the APK
# (AGP packages IMPORTED shared libs that targets link against).
file(GLOB_RECURSE _pron_extra_so "${CMAKE_CURRENT_BINARY_DIR}/engine/*/${ANDROID_ABI}/*.so"
     "${CMAKE_CURRENT_BINARY_DIR}/engine/*/lib/${ANDROID_ABI}/*.so")
set(_n 0)
foreach(so ${_pron_extra_so})
    get_filename_component(_nm "${so}" NAME_WE)
    if(NOT _nm MATCHES "^lib(pron_engine|pron_jni|c\\+\\+_shared)$")
        add_library(pron_extra_${_n} SHARED IMPORTED)
        set_target_properties(pron_extra_${_n} PROPERTIES IMPORTED_LOCATION "${so}")
        target_link_libraries(pron_jni PRIVATE pron_extra_${_n})
        math(EXPR _n "${_n}+1")
    endif()
endforeach()
