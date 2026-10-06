cmake_minimum_required(VERSION 3.14)  # Android uses 3.22.1; nothing newer than 3.14 is used here
project(pron_core VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PRON_BUILD_TESTS "Build pron_core unit tests" ON)
option(PRON_BUILD_SHARED "Also build pron_c shared library (DLL/.so) exporting the C API" OFF)

set(PRON_SOURCES
  src/text.cpp
  src/numbers.cpp
  src/word_align.cpp
  src/phonemes.cpp
  src/cmudict.cpp
  src/lexicon.cpp
  src/posteriors.cpp
  src/ctc_align.cpp
  src/gop.cpp
  src/fluency.cpp
  src/advice.cpp
  src/json_writer.cpp
  src/assessment.cpp
  src/result_json.cpp
  src/pron_c.cpp
)

function(pron_set_warnings tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# Static library: C++ API + C API. Used by tests and for static linking (Android JNI .so, Windows exe).
add_library(pron_core STATIC ${PRON_SOURCES})
target_include_directories(pron_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(pron_core PUBLIC cxx_std_17)
set_target_properties(pron_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
pron_set_warnings(pron_core)

# Optional shared library exporting only the C API (pron_c.h), e.g. pron.dll for C# P/Invoke.
if(PRON_BUILD_SHARED)
  add_library(pron_c SHARED ${PRON_SOURCES})
  target_include_directories(pron_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(pron_c PUBLIC PRON_SHARED PRIVATE PRON_BUILDING)
  set_target_properties(pron_c PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON OUTPUT_NAME pron)
  pron_set_warnings(pron_c)
endif()

if(PRON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
