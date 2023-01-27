## Copyright 2023 Jefferson Amstutz
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.11)

message(STATUS "CMake version: ${CMAKE_VERSION}")

if(${CMAKE_CURRENT_LIST_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
  set(DLAF_STANDALONE ON)
else()
  set(DLAF_STANDALONE OFF)
endif()

## CMake setup ##

include(cmake/cmake_project_commands.cmake)

if (DLAF_STANDALONE)
  set(CMAKE_DISABLE_SOURCE_CHANGES ON)
  set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  set(CMAKE_BUILD_TYPE_INIT Release)
  set(CMAKE_INSTALL_MESSAGE LAZY)

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

## Dependencies ##

find_package(Boost REQUIRED)

## Library ##

project(dlaf LANGUAGES CXX)
project_add_library(STATIC DLAF.cpp)
project_link_libraries(PUBLIC Boost::boost)
project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
)

if (NOT DLAF_STANDALONE)
  add_library(dlaf::dlaf ALIAS dlaf)
  return()
endif()

## Command line tool ##

project(compute_dlaf LANGUAGES CXX)
project_add_executable(compute_dlaf.cpp)
project_link_libraries(PRIVATE dlaf)

enable_testing()
