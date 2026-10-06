cmake_minimum_required(VERSION 3.18)
project(pron_asr_whisper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PRON_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../core/include" CACHE PATH "pron core include dir")
option(PRON_WHISPER_BUILD_TESTS "Build whisper backend smoke test" ON)

include(FetchContent)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper_cpp
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG v1.9.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(whisper_cpp)

add_library(pron_asr_whisper STATIC src/whisper_asr.cpp)
target_include_directories(pron_asr_whisper PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${PRON_CORE_INCLUDE_DIR}")
target_link_libraries(pron_asr_whisper PUBLIC whisper)

if(PRON_WHISPER_BUILD_TESTS)
    enable_testing()
    add_executable(whisper_smoke test/smoke.cpp)
    target_link_libraries(whisper_smoke PRIVATE pron_asr_whisper)
    add_test(NAME whisper_smoke
        COMMAND whisper_smoke "${whisper_cpp_SOURCE_DIR}/models/for-tests-ggml-tiny.en.bin")
    set_tests_properties(whisper_smoke PROPERTIES SKIP_RETURN_CODE 77)
endif()
