# Unit tests (doctest). A vendored copy of doctest.h is used when present; otherwise it is
# downloaded with FetchContent.
set(PRON_DOCTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest")
if(NOT EXISTS "${PRON_DOCTEST_DIR}/doctest.h")
  include(FetchContent)
  FetchContent_Declare(doctest
    URL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h
    DOWNLOAD_NO_EXTRACT TRUE)
  FetchContent_MakeAvailable(doctest)
  set(PRON_DOCTEST_DIR "${doctest_SOURCE_DIR}")
endif()

set(PRON_TEST_SUITES text word_align phonemes cmudict ctc gop fluency advice json assessment c_api)

set(PRON_TEST_SOURCES main.cpp)
foreach(s ${PRON_TEST_SUITES})
  list(APPEND PRON_TEST_SOURCES test_${s}.cpp)
endforeach()

add_executable(pron_tests ${PRON_TEST_SOURCES} c_header_check.c)
target_include_directories(pron_tests PRIVATE "${PRON_DOCTEST_DIR}")
target_link_libraries(pron_tests PRIVATE pron_core)
pron_set_warnings(pron_tests)

# One CTest entry per suite.
foreach(s ${PRON_TEST_SUITES})
  add_test(NAME ${s} COMMAND pron_tests --test-suite=${s})
endforeach()
