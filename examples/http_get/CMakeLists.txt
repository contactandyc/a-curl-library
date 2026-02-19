# SPDX-FileCopyrightText: 2026 Andy Curtis <contactandyc@gmail.com>
# SPDX-FileCopyrightText: 2024–2025 Knode.ai — technical questions: contact Andy (above)
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(http_get_examples LANGUAGES C)

# Let examples (and their transitive deps) pick an installed flavor.
# Pass -DA_BUILD_VARIANT=debug|memory|coverage|static|shared if you want to override.
set(A_BUILD_VARIANT "debug" CACHE STRING "Build variant (debug|memory|coverage|static|shared)")
set_property(CACHE A_BUILD_VARIANT PROPERTY STRINGS debug memory coverage static shared)

# The curl wrapper package (this should bring libcurl, threads, etc. transitively)
find_package(a_curl_library CONFIG REQUIRED)

# Optional system libs some platforms want
find_library(M_LIB  m)
find_library(RT_LIB rt)

# -------------------------------------------------------------------
# Helper: make_example(<name> <src>)
# -------------------------------------------------------------------
function(make_example name src)
  add_executable(${name} "${src}")
  set_target_properties(${name} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
  )

  # Link the umbrella target (carries transitive deps)
  target_link_libraries(${name} PRIVATE a_curl_library::a_curl_library)

  # Optional libs when they exist
  if(M_LIB)
    target_link_libraries(${name} PRIVATE ${M_LIB})
  endif()
  if(RT_LIB)
    target_link_libraries(${name} PRIVATE ${RT_LIB})
  endif()

  # Warnings (match the style you use in tests)
  if(MSVC)
    target_compile_options(${name} PRIVATE /W4)
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# -------------------------------------------------------------------
# Examples
# -------------------------------------------------------------------
make_example(basic_get  "${CMAKE_CURRENT_SOURCE_DIR}/src/basic_get.c")
make_example(fetch_list "${CMAKE_CURRENT_SOURCE_DIR}/src/fetch_list.c")

# (Optional) copy the data file next to the fetch_list executable for easy running:
add_custom_command(TARGET fetch_list POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/data/popular_urls.txt"
          "$<TARGET_FILE_DIR:fetch_list>/popular_urls.txt"
  COMMENT "Copying popular_urls.txt next to fetch_list"
)
