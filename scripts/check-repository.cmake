# SPDX-License-Identifier: Apache-2.0
# Validate the minimal source repository without documentation or Git dependencies.
cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED PROJECT_ROOT OR NOT DEFINED PROJECT_VERSION)
  message(FATAL_ERROR "Pass PROJECT_ROOT and PROJECT_VERSION")
endif()
foreach(path IN ITEMS CMakeLists.txt LICENSE .gitignore .gitattributes ClickNova.cmd
                      scripts/check-commit.ps1 scripts/package-source.ps1
                      scripts/build-windows.ps1 scripts/verify-source-tree.ps1
                      src/gui/main.cpp include/clicknova/version.hpp.in
                      scripts/build-linux.sh scripts/package-portable-linux.sh
                      scripts/package-appimage-linux.sh
                      scripts/package-appimage-bundle-linux.py
                      .github/workflows/portable-releases.yml
                      packaging/clicknova.desktop packaging/clicknova.svg
                      tests/release_packaging_tests.py
                      scripts/uninstall-clicknova.sh scripts/uninstall-clicknova.ps1
                      tests/uninstall_safety_tests.py Enable-OpenCV-OCR.cmd
                      tests/preflight_tests.cpp tests/x11_window_tests.cpp
                      src/platform/x11/window_target_x11.cpp
                      src/platform/x11/x11_input_backend.cpp
                      include/clicknova/x11_input_backend.hpp
                      tests/x11_input_tests.cpp
                      extensions/chromium/manifest.json extensions/chromium/background.js
                      extensions/firefox/manifest.json extensions/firefox/background.js)
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
