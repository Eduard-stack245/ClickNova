# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(ClickNova VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All source files are UTF-8 (including Russian UI strings and duration tests).
# Without this MSVC can silently replace Cyrillic in u"..." literals with '?'
# when the Windows ANSI codepage is not UTF-8. Apply to ALL targets, including
# Qt-independent test executables (ClickNova already used /utf-8 by itself).
if(MSVC)
  add_compile_options(/utf-8)
endif()

add_library(clicknova_core STATIC src/core.cpp src/window_guard.cpp src/sequence.cpp src/sequence_preview.cpp src/sequence_conversion.cpp src/sequence_to_input.cpp src/input_actions.cpp src/quick_motion.cpp src/input_recording.cpp src/recording_review.cpp)
target_include_directories(clicknova_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(clicknova_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(clicknova_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(clicknova_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# X11 native windows are optional. XTEST injection requires a separately
# acquired global emergency stop before any playback; never use Wayland fallback.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(X11 QUIET)
  if(X11_FOUND)
    message(STATUS "ClickNova Linux: X11 window discovery enabled")
    add_library(clicknova_input_x11 STATIC src/platform/x11/x11_input_backend.cpp)
    target_include_directories(clicknova_input_x11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(clicknova_input_x11 PUBLIC CLICKNOVA_WITH_X11=1)
    target_link_libraries(clicknova_input_x11 PUBLIC clicknova_core X11::X11 ${CMAKE_DL_LIBS})
    target_compile_options(clicknova_input_x11 PRIVATE -Wall -Wextra -Wpedantic)
    function(clicknova_enable_x11 target)
      target_sources(${target} PRIVATE src/platform/x11/window_target_x11.cpp)
      target_compile_definitions(${target} PRIVATE CLICKNOVA_WITH_X11=1)
      target_link_libraries(${target} PRIVATE X11::X11)
    endfunction()
  else()
    message(STATUS "ClickNova Linux: X11 not found, native window discovery disabled")
  endif()
endif()

# Visual recognition is optional: base ClickNova must still compile without
# large native dependencies. Install opencv4 and tesseract via vcpkg and
# reconfigure with that toolchain to enable image matching and OCR.
if(WIN32)
  find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
  if(OpenCV_FOUND)
    message(STATUS "ClickNova visual recognition: OpenCV enabled")
    find_package(Tesseract QUIET)
    if(TARGET Tesseract::libtesseract)
      set(CLICKNOVA_OCR_TARGET Tesseract::libtesseract)
    elseif(TARGET Tesseract::tesseract)
      set(CLICKNOVA_OCR_TARGET Tesseract::tesseract)
    endif()
  endif()
endif()

# The Win32 adapter is linked into the Windows GUI for guarded input playback.
# The library is compiled separately so unit tests never inject OS input.
if(WIN32)
  add_library(clicknova_input_win32 STATIC src/platform/windows/windows_input_backend.cpp src/platform/window_target.cpp src/platform/ui_element_search.cpp
    src/platform/msaa_accessibility.cpp src/platform/java_access_bridge.cpp
    src/platform/visual_recognition.cpp)
  target_include_directories(clicknova_input_win32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(clicknova_input_win32 PUBLIC clicknova_core user32 ole32 oleaut32 uiautomationcore oleacc)
  if(OpenCV_FOUND)
    target_include_directories(clicknova_input_win32 PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(clicknova_input_win32 PRIVATE ${OpenCV_LIBS} gdi32)
    target_compile_definitions(clicknova_input_win32 PRIVATE CLICKNOVA_WITH_OPENCV=1)
    if(CLICKNOVA_OCR_TARGET)
      target_link_libraries(clicknova_input_win32 PRIVATE ${CLICKNOVA_OCR_TARGET})
      target_compile_definitions(clicknova_input_win32 PRIVATE CLICKNOVA_WITH_TESSERACT=1)
      message(STATUS "ClickNova visual recognition: Tesseract OCR enabled")
    else()
      message(STATUS "ClickNova visual recognition: Tesseract absent; OCR disabled")
    endif()
  else()
    message(STATUS "ClickNova visual recognition: OpenCV absent; image/OCR disabled")
  endif()
  target_compile_features(clicknova_input_win32 PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(clicknova_input_win32 PRIVATE /W4 /permissive-)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  if(UNIX)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      add_test(NAME uninstall_safety_tests
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/uninstall_safety_tests.py)
      add_test(NAME visual_installer_contract_tests
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/visual_installer_contract_tests.py)
      if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME portable_release_packaging_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/release_packaging_tests.py)
      endif()
    endif()
  endif()
  add_executable(clicknova_core_tests tests/core_tests.cpp)
  target_link_libraries(clicknova_core_tests PRIVATE clicknova_core)
  add_test(NAME core_tests COMMAND clicknova_core_tests)
  add_executable(clicknova_action_step_groups_tests tests/action_step_groups_tests.cpp)
  target_link_libraries(clicknova_action_step_groups_tests PRIVATE clicknova_core)
  add_test(NAME action_step_groups_tests COMMAND clicknova_action_step_groups_tests)
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
  add_executable(clicknova_sequence_to_input_tests tests/sequence_to_input_tests.cpp)
  target_link_libraries(clicknova_sequence_to_input_tests PRIVATE clicknova_core)
  add_test(NAME sequence_to_input_tests COMMAND clicknova_sequence_to_input_tests)
  add_executable(clicknova_ui_element_search_tests tests/ui_element_search_tests.cpp src/platform/ui_element_search.cpp
    src/platform/msaa_accessibility.cpp src/platform/java_access_bridge.cpp
    src/platform/visual_recognition.cpp)
  target_link_libraries(clicknova_ui_element_search_tests PRIVATE clicknova_core)
  if(WIN32)
    # ui_element_search.cpp invokes platform::checkTarget() before UIA Invoke.
    # Link the actual Win32 window adapter into this standalone test target;
    # clicknova_core supplies the pure target evaluation policy. The test
    # deliberately never injects input or invokes a live UIA control.
    target_sources(clicknova_ui_element_search_tests PRIVATE src/platform/window_target.cpp)
    target_link_libraries(clicknova_ui_element_search_tests PRIVATE user32 ole32 oleaut32 uiautomationcore oleacc)
  endif()
  if(NOT WIN32)
    target_sources(clicknova_ui_element_search_tests PRIVATE src/platform/window_target.cpp)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    clicknova_enable_x11(clicknova_ui_element_search_tests)
  endif()
  add_test(NAME ui_element_search_tests COMMAND clicknova_ui_element_search_tests)
  add_executable(clicknova_visual_recognition_tests tests/visual_recognition_tests.cpp)
  if(WIN32)
    target_link_libraries(clicknova_visual_recognition_tests PRIVATE clicknova_input_win32)
  else()
    target_sources(clicknova_visual_recognition_tests PRIVATE
      src/platform/visual_recognition.cpp src/platform/ui_element_search.cpp
      src/platform/msaa_accessibility.cpp src/platform/java_access_bridge.cpp
      src/platform/window_target.cpp)
  endif()
  target_link_libraries(clicknova_visual_recognition_tests PRIVATE clicknova_core)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    clicknova_enable_x11(clicknova_visual_recognition_tests)
  endif()
  add_test(NAME visual_recognition_tests COMMAND clicknova_visual_recognition_tests)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    add_executable(clicknova_x11_input_tests tests/x11_input_tests.cpp src/platform/window_target.cpp)
    target_link_libraries(clicknova_x11_input_tests PRIVATE clicknova_input_x11)
    clicknova_enable_x11(clicknova_x11_input_tests)
    add_test(NAME x11_input_headless_tests COMMAND $<TARGET_FILE:clicknova_x11_input_tests> --headless)
    add_executable(clicknova_x11_window_tests tests/x11_window_tests.cpp src/platform/window_target.cpp)
    target_link_libraries(clicknova_x11_window_tests PRIVATE clicknova_core)
    clicknova_enable_x11(clicknova_x11_window_tests)
    add_test(NAME x11_headless_fallback_tests COMMAND $<TARGET_FILE:clicknova_x11_window_tests> --headless)
    find_program(CLICKNOVA_XVFB_RUN NAMES xvfb-run)
    if(CLICKNOVA_XVFB_RUN)
      add_test(NAME x11_window_discovery_tests COMMAND ${CLICKNOVA_XVFB_RUN} -a $<TARGET_FILE:clicknova_x11_window_tests>)
      add_test(NAME x11_input_emergency_tests COMMAND ${CLICKNOVA_XVFB_RUN} -a $<TARGET_FILE:clicknova_x11_input_tests>)
    endif()
  endif()

  add_executable(clicknova_step_window_tests tests/step_window_tests.cpp)
  target_link_libraries(clicknova_step_window_tests PRIVATE clicknova_core)
  add_test(NAME step_window_tests COMMAND clicknova_step_window_tests)
  add_executable(clicknova_universal_element_tasks_tests tests/universal_element_tasks_tests.cpp)
  target_link_libraries(clicknova_universal_element_tasks_tests PRIVATE clicknova_core)
  add_test(NAME universal_element_tasks_tests COMMAND clicknova_universal_element_tasks_tests)
  add_executable(clicknova_preflight_tests tests/preflight_tests.cpp)
  target_link_libraries(clicknova_preflight_tests PRIVATE clicknova_core)
  add_test(NAME preflight_tests COMMAND clicknova_preflight_tests)
  add_executable(clicknova_input_actions_tests tests/input_actions_tests.cpp)
  target_link_libraries(clicknova_input_actions_tests PRIVATE clicknova_core)
  add_executable(clicknova_action_list_edit_tests tests/action_list_edit_tests.cpp)
  target_link_libraries(clicknova_action_list_edit_tests PRIVATE clicknova_core)
  add_test(NAME action_list_edit_tests COMMAND clicknova_action_list_edit_tests)
  add_test(NAME input_actions_tests COMMAND clicknova_input_actions_tests)
  add_executable(clicknova_quick_motion_tests tests/quick_motion_tests.cpp)
  target_link_libraries(clicknova_quick_motion_tests PRIVATE clicknova_core)
  add_test(NAME quick_motion_tests COMMAND clicknova_quick_motion_tests)
  add_executable(clicknova_duration_expression_tests tests/duration_expression_tests.cpp)
  target_include_directories(clicknova_duration_expression_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME duration_expression_tests COMMAND clicknova_duration_expression_tests)
  add_executable(clicknova_start_delay_tests tests/start_delay_tests.cpp)
  target_include_directories(clicknova_start_delay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  add_test(NAME start_delay_tests COMMAND clicknova_start_delay_tests)
  add_executable(clicknova_input_recording_tests tests/input_recording_tests.cpp)
  target_link_libraries(clicknova_input_recording_tests PRIVATE clicknova_core)
  add_test(NAME input_recording_tests COMMAND clicknova_input_recording_tests)
  add_executable(clicknova_recording_review_tests tests/recording_review_tests.cpp)
  target_link_libraries(clicknova_recording_review_tests PRIVATE clicknova_core)
  add_test(NAME recording_review_tests COMMAND clicknova_recording_review_tests)
  add_test(NAME repository_consistency
      COMMAND ${CMAKE_COMMAND} "-DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
              "-DPROJECT_VERSION=${PROJECT_VERSION}"
              -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-repository.cmake)
  add_test(NAME license_consistency
      COMMAND ${CMAKE_COMMAND} "-DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
              -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-source-license.cmake)
endif()

# Optional native Qt application. The reusable C++20 core stays Qt-free.
option(CLICKNOVA_BUILD_GUI "Build the Qt 6 Widgets desktop app" OFF)
if(CLICKNOVA_BUILD_GUI)
  find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Network)
  qt_standard_project_setup()
  if(BUILD_TESTING)
    add_executable(clicknova_portable_storage_tests tests/portable_storage_tests.cpp src/gui/portable_storage.cpp)
    target_link_libraries(clicknova_portable_storage_tests PRIVATE Qt6::Core)
    add_test(NAME portable_storage_tests COMMAND clicknova_portable_storage_tests)
  endif()
  if(BUILD_TESTING)
    add_executable(clicknova_browser_discovery_tests tests/browser_discovery_tests.cpp src/gui/browser_discovery.cpp)
    target_link_libraries(clicknova_browser_discovery_tests PRIVATE Qt6::Core)
    add_test(NAME browser_discovery_tests COMMAND clicknova_browser_discovery_tests)
    add_executable(clicknova_browser_extension_bridge_tests
      tests/browser_extension_bridge_tests.cpp
      src/gui/browser_discovery.cpp
      src/gui/browser_extension_bridge.cpp)
    target_link_libraries(clicknova_browser_extension_bridge_tests PRIVATE Qt6::Core Qt6::Network)
    add_test(NAME browser_extension_bridge_tests COMMAND clicknova_browser_extension_bridge_tests)
  endif()

  # This is the single source of the app version used in the title bar.
  configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/include/clicknova/version.hpp.in
      ${CMAKE_CURRENT_BINARY_DIR}/generated/clicknova/version.hpp
      @ONLY)

  qt_add_executable(ClickNova WIN32
    src/gui/main.cpp
    src/gui/portable_storage.cpp
    src/gui/portable_storage.hpp
    src/gui/optional_components.cpp
    src/gui/optional_components.hpp
    src/gui/app_style.cpp
    src/gui/app_style.hpp
    src/gui/ui_icons.cpp
    src/gui/ui_icons.hpp
    src/gui/smoke_tests.cpp
    src/gui/smoke_tests.hpp
    src/gui/wrap_layout.cpp
    src/gui/wrap_layout.hpp
    src/gui/wheel_scroll_filter.cpp
    src/gui/wheel_scroll_filter.hpp
    src/gui/main_window.cpp
    src/gui/main_window.hpp
    src/gui/action_table_widget.hpp
    src/gui/input_lab.cpp
    src/gui/browser_discovery.cpp
    src/gui/browser_discovery.hpp
    src/gui/browser_extension_bridge.cpp
    src/gui/browser_extension_bridge.hpp
    src/gui/input_capture_dialog.cpp
    src/gui/input_recorder_dialog.cpp
    src/gui/recording_checkpoint.cpp
    src/gui/recording_checkpoint.hpp
    src/gui/input_recorder_dialog.hpp
    src/gui/recording_review_dialog.cpp
    src/gui/recording_review_dialog.hpp
    src/gui/input_capture_dialog.hpp
    src/gui/input_lab.hpp
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
    src/storage/input_plan_codec.cpp
    src/storage/input_plan_codec.hpp
    src/storage/scenario_codec.cpp
    src/storage/scenario_store.hpp
    src/platform/windows/input_support.hpp)
  target_include_directories(ClickNova PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_link_libraries(ClickNova PRIVATE Qt6::Widgets Qt6::Network clicknova_core)
  if(OpenCV_FOUND)
    target_compile_definitions(ClickNova PRIVATE CLICKNOVA_WITH_OPENCV=1)
    if(CLICKNOVA_OCR_TARGET)
      target_compile_definitions(ClickNova PRIVATE CLICKNOVA_WITH_TESSERACT=1)
    endif()
  endif()
  if(WIN32)
    target_link_libraries(ClickNova PRIVATE user32 clicknova_input_win32)
  else()
    target_sources(ClickNova PRIVATE src/platform/window_target.cpp src/platform/ui_element_search.cpp
      src/platform/msaa_accessibility.cpp src/platform/java_access_bridge.cpp
    src/platform/visual_recognition.cpp)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    clicknova_enable_x11(ClickNova)
    target_link_libraries(ClickNova PRIVATE clicknova_input_x11)
  endif()
  target_compile_features(ClickNova PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(ClickNova PRIVATE /W4 /permissive-)
  else()
    target_compile_options(ClickNova PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  include(GNUInstallDirs)
  install(TARGETS ClickNova
      BUNDLE DESTINATION .
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # Ship both extension variants with the Windows distribution. Browsers
  # still require explicit installation/consent; nothing is side-loaded.
  install(DIRECTORY extensions/chromium/
      DESTINATION ${CMAKE_INSTALL_BINDIR}/extensions/chromium
      FILES_MATCHING PATTERN "*.js" PATTERN "*.json" PATTERN "*.html")
  install(DIRECTORY extensions/firefox/
      DESTINATION ${CMAKE_INSTALL_BINDIR}/extensions/firefox
      FILES_MATCHING PATTERN "*.js" PATTERN "*.json" PATTERN "*.html")
  qt_generate_deploy_app_script(
      TARGET ClickNova
      OUTPUT_SCRIPT clicknova_deploy_script)
  install(SCRIPT ${clicknova_deploy_script})
  if(BUILD_TESTING)
    # Layout regression after resizing the main window to its minimum size.
    add_test(NAME gui_layout_smoke COMMAND ClickNova --smoke-test-ui)
    add_test(NAME gui_adaptive_smoke COMMAND ClickNova --smoke-test-adaptive)
    add_test(NAME gui_icons_smoke COMMAND ClickNova --smoke-test-icons)
    add_test(NAME gui_language_smoke COMMAND ClickNova --smoke-test-language)
    add_test(NAME gui_workflows_smoke COMMAND ClickNova --smoke-test-workflows)
    add_test(NAME gui_sequence_smoke COMMAND ClickNova --smoke-test-sequence)
    add_test(NAME gui_input_lab_smoke COMMAND ClickNova --smoke-test-input-lab)
    add_test(NAME gui_unified_smoke COMMAND ClickNova --smoke-test-unified)
    add_test(NAME gui_unified_search_smoke COMMAND ClickNova --smoke-test-unified-search)
    add_test(NAME gui_step_tree_smoke COMMAND ClickNova --smoke-test-step-tree)
    add_test(NAME gui_manual_step_groups_smoke COMMAND ClickNova --smoke-test-manual-step-groups)
    add_test(NAME gui_wheel_routing_smoke COMMAND ClickNova --smoke-test-wheel-routing)
    add_test(NAME gui_action_menu_smoke COMMAND ClickNova --smoke-test-action-menu)
    add_test(NAME gui_recorder_smoke COMMAND ClickNova --smoke-test-recorder)
    add_test(NAME gui_recording_recovery_smoke COMMAND ClickNova --smoke-test-recording-recovery)
    add_test(NAME gui_recording_review_smoke COMMAND ClickNova --smoke-test-recording-review)
    add_test(NAME gui_quick_motion_smoke COMMAND ClickNova --smoke-test-quick-motion)
    add_test(NAME gui_window_picker_smoke COMMAND ClickNova --smoke-test-window-picker)
    add_test(NAME gui_scenarios_smoke COMMAND ClickNova --smoke-test-scenarios)
    add_executable(clicknova_scenario_store_tests
        tests/scenario_store_tests.cpp
        src/storage/scenario_store.cpp
        src/gui/portable_storage.cpp
        src/storage/scenario_codec.cpp
        src/i18n/translations.cpp
        src/i18n/en/dictionary.cpp
        src/i18n/ru/dictionary.cpp)
    target_link_libraries(clicknova_scenario_store_tests PRIVATE Qt6::Core clicknova_core)
    add_test(NAME scenario_store_tests COMMAND clicknova_scenario_store_tests)
    add_executable(clicknova_input_plan_codec_tests
        tests/input_plan_codec_tests.cpp
        src/storage/input_plan_codec.cpp
        src/storage/scenario_store.cpp
        src/gui/portable_storage.cpp)
    target_link_libraries(clicknova_input_plan_codec_tests PRIVATE Qt6::Core clicknova_core)
    add_test(NAME input_plan_codec_tests COMMAND clicknova_input_plan_codec_tests)
    if(WIN32)
      add_test(NAME gui_shortcuts_smoke COMMAND ClickNova --smoke-test-shortcuts)
    endif()
  endif()
endif()
