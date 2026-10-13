# SPDX-License-Identifier: Apache-2.0
# Validate the minimal source repository without documentation or Git dependencies.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR "Pass PROJECT_ROOT and PROJECT_VERSION")
endif()
foreach(path IN ITEMS CMakeLists.txt LICENSE .gitignore .gitattributes ClickNova.cmd
                      scripts/check-commit.ps1 scripts/package-source.ps1
                      scripts/build-windows.ps1 scripts/verify-source-tree.ps1
                      src/gui/main.cpp include/clicknova/version.hpp.in)
  if(NOT EXISTS "${PROJECT_ROOT}/${path}")
    message(FATAL_ERROR "Required source file is missing: ${path}")
  endif()
endforeach()
file(READ "${PROJECT_ROOT}/CMakeLists.txt" cmake_file)
string(REGEX MATCH "project\\(ClickNova VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" version_in_cmake "${cmake_file}")
if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR "CMake and requested version differ: '${CMAKE_MATCH_1}' != '${PROJECT_VERSION}'")
endif()
message(STATUS "ClickNova ${PROJECT_VERSION}: source repository consistency passed")
