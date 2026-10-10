# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(ClickNova VERSION 0.5.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(clicknova_core STATIC src/core.cpp src/window_guard.cpp src/sequence.cpp src/sequence_preview.cpp src/sequence_conversion.cpp)
target_include_directories(clicknova_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(clicknova_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(clicknova_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(clicknova_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(clicknova_core_tests tests/core_tests.cpp)
  target_link_libraries(clicknova_core_tests PRIVATE clicknova_core)
  add_test(NAME core_tests COMMAND clicknova_core_tests)
  add_executable(clicknova_window_guard_tests tests/window_guard_tests.cpp)
  target_link_libraries(clicknova_window_guard_tests PRIVATE clicknova_core)
  add_test(NAME window_guard_tests COMMAND clicknova_window_guard_tests)
  add_executable(clicknova_sequence_tests tests/sequence_tests.cpp)
  target_link_libraries(clicknova_sequence_tests PRIVATE clicknova_core)
  add_test(NAME sequence_tests COMMAND clicknova_sequence_tests)
  add_executable(clicknova_sequence_preview_tests tests/sequence_preview_tests.cpp)
  target_link_libraries(clicknova_sequence_preview_tests PRIVATE clicknova_core)
  add_test(NAME sequence_preview_tests COMMAND clicknova_sequence_preview_tests)
  add_executable(clicknova_sequence_conversion_tests tests/sequence_conversion_tests.cpp)
  target_link_libraries(clicknova_sequence_conversion_tests PRIVATE clicknova_core)
  add_test(NAME sequence_conversion_tests COMMAND clicknova_sequence_conversion_tests)
  add_test(NAME license_consistency
      COMMAND ${CMAKE_COMMAND} "-DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
              -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-source-license.cmake)
endif()

# Optional native Qt application. The reusable C++20 core stays Qt-free.
option(CLICKNOVA_BUILD_GUI "Build the experimental Qt 6 Widgets desktop app" OFF)
if(CLICKNOVA_BUILD_GUI)
  find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets)
  qt_standard_project_setup()

  # This is the single source of the app version used in the title bar.
  configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/include/clicknova/version.hpp.in
      ${CMAKE_CURRENT_BINARY_DIR}/generated/clicknova/version.hpp
      @ONLY)

  qt_add_executable(ClickNova WIN32
    src/gui/main.cpp
    src/gui/app_style.cpp
    src/gui/app_style.hpp
    src/gui/smoke_tests.cpp
    src/gui/smoke_tests.hpp
    src/gui/wrap_layout.cpp
    src/gui/wrap_layout.hpp
    src/gui/wheel_scroll_filter.cpp
    src/gui/wheel_scroll_filter.hpp
    src/gui/main_window.cpp
    src/gui/main_window.hpp
    src/gui/sequence_editor.cpp
    src/gui/ui_translation.hpp
    src/gui/sequence_editor.hpp
    src/gui/window_chooser.hpp
    src/gui/window_chooser.cpp
    src/i18n/translations.cpp
    src/i18n/translations.hpp
    src/i18n/en/dictionary.cpp
    src/i18n/ru/dictionary.cpp
    src/storage/scenario_store.cpp
    src/storage/scenario_codec.cpp
    src/storage/scenario_store.hpp
    src/platform/window_target.cpp
    src/platform/windows/input_support.hpp)
  target_include_directories(ClickNova PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_link_libraries(ClickNova PRIVATE Qt6::Widgets clicknova_core)
  if(WIN32)
    target_link_libraries(ClickNova PRIVATE user32)
  endif()
  target_compile_features(ClickNova PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(ClickNova PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(ClickNova PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  include(GNUInstallDirs)
  install(TARGETS ClickNova
      BUNDLE DESTINATION .
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  qt_generate_deploy_app_script(
      TARGET ClickNova
      OUTPUT_SCRIPT clicknova_deploy_script)
  install(SCRIPT ${clicknova_deploy_script})
  if(BUILD_TESTING)
    # Layout regression after resizing the main window to its minimum size.
    add_test(NAME gui_layout_smoke COMMAND ClickNova --smoke-test-ui)
    add_test(NAME gui_adaptive_smoke COMMAND ClickNova --smoke-test-adaptive)
    add_test(NAME gui_language_smoke COMMAND ClickNova --smoke-test-language)
    add_test(NAME gui_workflows_smoke COMMAND ClickNova --smoke-test-workflows)
    add_test(NAME gui_sequence_smoke COMMAND ClickNova --smoke-test-sequence)
    add_test(NAME gui_window_picker_smoke COMMAND ClickNova --smoke-test-window-picker)
    add_test(NAME gui_scenarios_smoke COMMAND ClickNova --smoke-test-scenarios)
    add_executable(clicknova_scenario_store_tests
        tests/scenario_store_tests.cpp
        src/storage/scenario_store.cpp
        src/storage/scenario_codec.cpp
        src/i18n/translations.cpp
        src/i18n/en/dictionary.cpp
        src/i18n/ru/dictionary.cpp)
    target_link_libraries(clicknova_scenario_store_tests PRIVATE Qt6::Core clicknova_core)
    add_test(NAME scenario_store_tests COMMAND clicknova_scenario_store_tests)
    if(WIN32)
      add_test(NAME gui_shortcuts_smoke COMMAND ClickNova --smoke-test-shortcuts)
    endif()
  endif()
endif()
