cmake_minimum_required(VERSION 3.18)
project(pron_engine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PRON_ENGINE_BUILD_TESTS "Build engine tests" ON)
# Pinned versions (see README.md). sherpa-onnx ships its own onnxruntime: the whole engine uses THAT one.
set(SHERPA_ONNX_VERSION "1.12.14" CACHE STRING "sherpa-onnx release")
set(SHERPA_ORT_VERSION "1.17.1" CACHE STRING "onnxruntime version bundled in that sherpa-onnx release (headers are taken from it)")
set(SHERPA_ONNX_ROOT "" CACHE PATH "Unpacked sherpa-onnx shared release (include/, lib/); downloaded if empty")
set(ONNXRUNTIME_HEADERS_ROOT "" CACHE PATH "Unpacked onnxruntime release of SHERPA_ORT_VERSION (include/); downloaded if empty")

set(_deps "${CMAKE_BINARY_DIR}/_pron_deps")
set(_gh "https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_ONNX_VERSION}")

# Downloads <url> and unpacks it to <dir> (cached); sets <out> to the single top-level folder inside.
function(pron_fetch url dir out)
  if(NOT EXISTS "${dir}/.done")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    get_filename_component(_fn "${url}" NAME)
    message(STATUS "Downloading ${url}")
    file(DOWNLOAD "${url}" "${dir}/${_fn}" STATUS _st TLS_VERIFY ON)
    list(GET _st 0 _rc)
    if(NOT _rc EQUAL 0)
      file(REMOVE_RECURSE "${dir}")
      message(FATAL_ERROR "download failed: ${url}: ${_st}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${dir}/${_fn}" DESTINATION "${dir}/x")
    file(REMOVE "${dir}/${_fn}")
    file(WRITE "${dir}/.done" "")
  endif()
  file(GLOB _top LIST_DIRECTORIES true "${dir}/x/*")
  list(GET _top 0 _t)
  set(${out} "${_t}" PARENT_SCOPE)
endfunction()

# ---- sherpa-onnx (TTS + the one onnxruntime) --------------------------------------------------
if(NOT SHERPA_ONNX_ROOT)
  if(ANDROID)
    # Android archive has jniLibs/<abi>/{libonnxruntime.so,libsherpa-onnx-jni.so}; C headers come from the linux archive.
    pron_fetch("${_gh}/sherpa-onnx-v${SHERPA_ONNX_VERSION}-android.tar.bz2" "${_deps}/sherpa-android" _and)
    pron_fetch("${_gh}/sherpa-onnx-v${SHERPA_ONNX_VERSION}-linux-x64-shared.tar.bz2" "${_deps}/sherpa-linux-x64" _lin)
    set(_root "${_deps}/sherpa-android-root-${ANDROID_ABI}")
    file(MAKE_DIRECTORY "${_root}")
    file(COPY "${_and}/jniLibs/${ANDROID_ABI}/" DESTINATION "${_root}/lib")
    file(COPY "${_lin}/include" DESTINATION "${_root}")
    set(SHERPA_ONNX_ROOT "${_root}")
  elseif(WIN32)
    pron_fetch("${_gh}/sherpa-onnx-v${SHERPA_ONNX_VERSION}-win-x64-shared.tar.bz2" "${_deps}/sherpa-win-x64" _root)
    set(SHERPA_ONNX_ROOT "${_root}")
  else()
    pron_fetch("${_gh}/sherpa-onnx-v${SHERPA_ONNX_VERSION}-linux-x64-shared.tar.bz2" "${_deps}/sherpa-linux-x64" _root)
    set(SHERPA_ONNX_ROOT "${_root}")
  endif()
endif()
set(SHERPA_ONNX_ROOT "${SHERPA_ONNX_ROOT}" CACHE PATH "" FORCE)

# ---- onnxruntime: library from sherpa, headers from the matching official release ---------------
if(NOT ONNXRUNTIME_HEADERS_ROOT)
  pron_fetch("https://github.com/microsoft/onnxruntime/releases/download/v${SHERPA_ORT_VERSION}/onnxruntime-linux-x64-${SHERPA_ORT_VERSION}.tgz"
             "${_deps}/ort-headers-${SHERPA_ORT_VERSION}" ONNXRUNTIME_HEADERS_ROOT)
endif()
find_library(_ort_lib NAMES onnxruntime HINTS "${SHERPA_ONNX_ROOT}/lib" NO_DEFAULT_PATH)
if(NOT _ort_lib)
  message(FATAL_ERROR "onnxruntime not found in ${SHERPA_ONNX_ROOT}/lib")
endif()
set(ORT_LIB "${_ort_lib}" CACHE FILEPATH "" FORCE)
set(ORT_INCLUDE_DIR "${ONNXRUNTIME_HEADERS_ROOT}/include" CACHE PATH "" FORCE)

# ---- subprojects --------------------------------------------------------------------------------
set(PRON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PRON_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PRON_WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PRON_ONNX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_BINARY_DIR}/core")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../backends/whisper" "${CMAKE_BINARY_DIR}/backends/whisper")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../backends/onnx" "${CMAKE_BINARY_DIR}/backends/onnx")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../backends/tts" "${CMAKE_BINARY_DIR}/backends/tts")

# core sources are compiled straight into the shared lib so that pron_c.h gets exported too
get_target_property(_core_src pron_core SOURCES)
get_target_property(_core_dir pron_core SOURCE_DIR)
set(_core_abs "")
foreach(s ${_core_src})
  list(APPEND _core_abs "${_core_dir}/${s}")
endforeach()

add_library(pron_engine SHARED ${_core_abs} src/engine.cpp)
target_include_directories(pron_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" "${_core_dir}/include")
target_compile_definitions(pron_engine PUBLIC PRON_SHARED PRIVATE PRON_BUILDING)
set_target_properties(pron_engine PROPERTIES
  OUTPUT_NAME pron_engine
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
target_link_libraries(pron_engine PRIVATE pron_asr_whisper pron_onnx pron_tts)
if(MSVC)
  target_compile_options(pron_engine PRIVATE /W4 /utf-8)
else()
  target_compile_options(pron_engine PRIVATE -Wall -Wextra)
  target_link_options(pron_engine PRIVATE -Wl,--exclude-libs,ALL)
endif()

# ship sherpa-onnx + onnxruntime shared libs next to pron_engine
file(GLOB _rt "${SHERPA_ONNX_ROOT}/lib/*.so" "${SHERPA_ONNX_ROOT}/lib/*.dll" "${SHERPA_ONNX_ROOT}/bin/*.dll")
foreach(f ${_rt})
  get_filename_component(_n "${f}" NAME)
  if(_n MATCHES "cxx-api|jni")
    continue()
  endif()
  add_custom_command(TARGET pron_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${f}" "$<TARGET_FILE_DIR:pron_engine>")
endforeach()

if(PRON_ENGINE_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  FetchContent_GetProperties(whisper_cpp SOURCE_DIR _whisper_src)
  add_executable(pron_engine_tests tests/engine_tests.cpp)
  target_link_libraries(pron_engine_tests PRIVATE pron_engine)
  set(_td "${CMAKE_BINARY_DIR}/engine_test_data")
  # models dir assembled for the test: vad/silero_vad.onnx, tts/us/* (voice cached in build/voice)
  add_test(NAME engine_fetch_models COMMAND ${CMAKE_COMMAND}
    -DDATA=${_td} -DVOICE=${CMAKE_BINARY_DIR}/voice
    -DVOICE_URL=${PRON_TTS_VOICE_URL}
    -DVOICE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/../backends/tts/cmake/fetch_voice.cmake
    -DCMUDICT=${CMAKE_CURRENT_SOURCE_DIR}/../models/cmudict.dict
    -DWHISPER_TEST_MODEL=${_whisper_src}/models/for-tests-ggml-tiny.en.bin
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fetch_models.cmake)
  add_test(NAME engine_tests COMMAND pron_engine_tests ${_td})
  set_tests_properties(engine_tests PROPERTIES DEPENDS engine_fetch_models)
endif()
