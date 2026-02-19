# SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
# SPDX-License-Identifier: Apache-2.0

# CMakeLists.txt for tests
cmake_minimum_required(VERSION 3.20)

project(a_curl_library_tests LANGUAGES C)

set(A_BUILD_VARIANT "debug" CACHE STRING
    "Variant to link via a_curl_library::a_curl_library (debug|memory|static|shared)")
set_property(CACHE A_BUILD_VARIANT PROPERTY STRINGS debug memory static shared)

option(A_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

find_library(M_LIB m)

# ---- Test executables ----
set(TEST_EXECUTABLES "")
add_executable(test_curl_resource  src/test_curl_resource.c)

list(APPEND TEST_EXECUTABLES test_curl_resource)

set_target_properties(test_curl_resource PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_curl_resource PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_curl_resource PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_curl_resource PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_curl_resource PRIVATE /W4)
else()
  target_compile_options(test_curl_resource PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_curl_resource PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_curl_resource PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_curl_resource PRIVATE -O0 -g --coverage)
    target_link_options(test_curl_resource PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_curl_resource COMMAND $<TARGET_FILE:test_curl_resource>)
add_executable(test_curl_resource_async  src/test_curl_resource_async.c)

list(APPEND TEST_EXECUTABLES test_curl_resource_async)

set_target_properties(test_curl_resource_async PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_curl_resource_async PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_curl_resource_async PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_curl_resource_async PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_curl_resource_async PRIVATE /W4)
else()
  target_compile_options(test_curl_resource_async PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_curl_resource_async PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_curl_resource_async PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_curl_resource_async PRIVATE -O0 -g --coverage)
    target_link_options(test_curl_resource_async PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_curl_resource_async COMMAND $<TARGET_FILE:test_curl_resource_async>)
add_executable(test_event_loop_cancel  src/test_event_loop_cancel.c)

list(APPEND TEST_EXECUTABLES test_event_loop_cancel)

set_target_properties(test_event_loop_cancel PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_event_loop_cancel PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_event_loop_cancel PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_event_loop_cancel PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_event_loop_cancel PRIVATE /W4)
else()
  target_compile_options(test_event_loop_cancel PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_event_loop_cancel PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_event_loop_cancel PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_event_loop_cancel PRIVATE -O0 -g --coverage)
    target_link_options(test_event_loop_cancel PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_event_loop_cancel COMMAND $<TARGET_FILE:test_event_loop_cancel>)
add_executable(test_event_loop_priority  src/test_event_loop_priority.c)

list(APPEND TEST_EXECUTABLES test_event_loop_priority)

set_target_properties(test_event_loop_priority PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_event_loop_priority PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_event_loop_priority PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_event_loop_priority PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_event_loop_priority PRIVATE /W4)
else()
  target_compile_options(test_event_loop_priority PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_event_loop_priority PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_event_loop_priority PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_event_loop_priority PRIVATE -O0 -g --coverage)
    target_link_options(test_event_loop_priority PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_event_loop_priority COMMAND $<TARGET_FILE:test_event_loop_priority>)
add_executable(test_rate_manager  src/test_rate_manager.c)

list(APPEND TEST_EXECUTABLES test_rate_manager)

set_target_properties(test_rate_manager PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_rate_manager PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_rate_manager PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_rate_manager PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_rate_manager PRIVATE /W4)
else()
  target_compile_options(test_rate_manager PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_rate_manager PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_rate_manager PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_rate_manager PRIVATE -O0 -g --coverage)
    target_link_options(test_rate_manager PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_rate_manager COMMAND $<TARGET_FILE:test_rate_manager>)
add_executable(test_rate_manager_hp_429  src/test_rate_manager_hp_429.c)

list(APPEND TEST_EXECUTABLES test_rate_manager_hp_429)

set_target_properties(test_rate_manager_hp_429 PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_rate_manager_hp_429 PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_rate_manager_hp_429 PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_rate_manager_hp_429 PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_rate_manager_hp_429 PRIVATE /W4)
else()
  target_compile_options(test_rate_manager_hp_429 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_rate_manager_hp_429 PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_rate_manager_hp_429 PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_rate_manager_hp_429 PRIVATE -O0 -g --coverage)
    target_link_options(test_rate_manager_hp_429 PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_rate_manager_hp_429 COMMAND $<TARGET_FILE:test_rate_manager_hp_429>)
add_executable(test_request_headers  src/test_request_headers.c)

list(APPEND TEST_EXECUTABLES test_request_headers)

set_target_properties(test_request_headers PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_request_headers PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_request_headers PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_request_headers PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_request_headers PRIVATE /W4)
else()
  target_compile_options(test_request_headers PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_request_headers PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_request_headers PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_request_headers PRIVATE -O0 -g --coverage)
    target_link_options(test_request_headers PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_request_headers COMMAND $<TARGET_FILE:test_request_headers>)
add_executable(test_request_json  src/test_request_json.c)

list(APPEND TEST_EXECUTABLES test_request_json)

set_target_properties(test_request_json PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_request_json PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_request_json PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_request_json PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_request_json PRIVATE /W4)
else()
  target_compile_options(test_request_json PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_request_json PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_request_json PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_request_json PRIVATE -O0 -g --coverage)
    target_link_options(test_request_json PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_request_json COMMAND $<TARGET_FILE:test_request_json>)
add_executable(test_sinks  src/test_sinks.c)

list(APPEND TEST_EXECUTABLES test_sinks)

set_target_properties(test_sinks PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_sinks PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_sinks PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_sinks PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_sinks PRIVATE /W4)
else()
  target_compile_options(test_sinks PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_sinks PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_sinks PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_sinks PRIVATE -O0 -g --coverage)
    target_link_options(test_sinks PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_sinks COMMAND $<TARGET_FILE:test_sinks>)
add_executable(test_worker_pool  src/test_worker_pool.c)

list(APPEND TEST_EXECUTABLES test_worker_pool)

set_target_properties(test_worker_pool PROPERTIES
  C_STANDARD 23
  C_STANDARD_REQUIRED YES
)
if("CXX" IN_LIST CMAKE_PROJECT_LANGUAGES)
  set_target_properties(test_worker_pool PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
  )
endif()

if(NOT TARGET a_curl_library::a_curl_library)
  find_package(a_curl_library CONFIG REQUIRED)
endif()
target_link_libraries(test_worker_pool PRIVATE a_curl_library::a_curl_library)

if(M_LIB)
  target_link_libraries(test_worker_pool PRIVATE ${M_LIB})
endif()

if(MSVC)
  target_compile_options(test_worker_pool PRIVATE /W4)
else()
  target_compile_options(test_worker_pool PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(A_ENABLE_COVERAGE)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_worker_pool PRIVATE -O0 -g -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(test_worker_pool PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  elseif (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_worker_pool PRIVATE -O0 -g --coverage)
    target_link_options(test_worker_pool PRIVATE --coverage)
  endif()
endif()

add_test(NAME test_worker_pool COMMAND $<TARGET_FILE:test_worker_pool>)

enable_testing()

# ---- Coverage aggregation ----
add_custom_target(coverage_report COMMENT "Generate coverage report")

if(A_ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
  include(ProcessorCount)
  ProcessorCount(NPROC)
  if(NPROC EQUAL 0)
    set(NPROC 4)
  endif()

  add_custom_command(TARGET coverage_report PRE_BUILD
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j ${NPROC}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running tests with coverage instrumentation"
  )

  find_program(LLVM_PROFDATA_EXECUTABLE llvm-profdata)
  find_program(LLVM_COV_EXECUTABLE llvm-cov)
  if(LLVM_PROFDATA_EXECUTABLE AND LLVM_COV_EXECUTABLE)
    add_custom_command(TARGET coverage_report POST_BUILD
      COMMAND ${LLVM_PROFDATA_EXECUTABLE} merge -sparse *.profraw -o default.profdata
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Merging coverage data"
    )

    # Normal compiled library path
    add_custom_command(TARGET coverage_report POST_BUILD
      COMMAND ${LLVM_COV_EXECUTABLE} show
              ../liba_curl_library_${A_BUILD_VARIANT}.a
              -instr-profile=default.profdata
              -format=html -output-dir=coverage_html
              --ignore-filename-regex='/usr/.*'
              --show-regions --show-line-counts-or-regions
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Generated LLVM coverage report → open coverage_html/index.html"
    )
  endif()
endif()
