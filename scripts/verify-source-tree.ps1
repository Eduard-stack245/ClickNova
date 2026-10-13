# SPDX-License-Identifier: Apache-2.0
# Checks required source files; does not modify the project.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$required = @(
    'CMakeLists.txt', 'LICENSE', '.gitignore', '.gitattributes', 'ClickNova.cmd',
    'include/clicknova/action_list_edit.hpp', 'tests/action_list_edit_tests.cpp',
    'src/gui/action_table_widget.hpp',
    'include/clicknova/core.hpp', 'include/clicknova/input_actions.hpp', 'include/clicknova/windows_input_backend.hpp', 'include/clicknova/sequence.hpp',
    'include/clicknova/windows_awake.hpp', 'include/clicknova/quick_motion.hpp', 'src/quick_motion.cpp', 'tests/quick_motion_tests.cpp',
    'include/clicknova/sequence_to_input.hpp', 'src/sequence_to_input.cpp', 'tests/sequence_to_input_tests.cpp',
    'include/clicknova/sequence_preview.hpp', 'include/clicknova/sequence_conversion.hpp', 'include/clicknova/version.hpp.in',
    'src/core.cpp', 'src/storage/input_plan_codec.cpp', 'src/storage/input_plan_codec.hpp', 'tests/input_plan_codec_tests.cpp', 'src/input_actions.cpp', 'src/platform/windows/windows_input_backend.cpp', 'src/window_guard.cpp', 'src/sequence.cpp', 'src/sequence_preview.cpp', 'src/sequence_conversion.cpp',
    'src/platform/window_target.cpp',
    'src/gui/main.cpp', 'src/gui/app_style.cpp', 'src/gui/app_style.hpp', 'src/gui/ui_icons.cpp', 'src/gui/ui_icons.hpp',
    'src/gui/smoke_tests.cpp', 'src/gui/smoke_tests.hpp',
    'src/gui/wrap_layout.cpp', 'src/gui/wrap_layout.hpp',
    'src/gui/wheel_scroll_filter.cpp', 'src/gui/wheel_scroll_filter.hpp',
    'src/gui/window_chooser.cpp', 'src/gui/window_chooser.hpp',
    'src/gui/main_window.cpp', 'src/gui/main_window.hpp', 'src/gui/input_lab.cpp', 'src/gui/input_lab.hpp',
    'src/gui/input_capture_dialog.cpp', 'src/gui/input_capture_dialog.hpp',
    'src/gui/input_recorder_dialog.cpp', 'src/gui/input_recorder_dialog.hpp', 'src/gui/recording_checkpoint.cpp', 'src/gui/recording_checkpoint.hpp',
    'src/gui/recording_review_dialog.cpp', 'src/gui/recording_review_dialog.hpp',
    'include/clicknova/recording_review.hpp', 'src/recording_review.cpp', 'tests/recording_review_tests.cpp',
    'include/clicknova/input_recording.hpp', 'src/input_recording.cpp', 'tests/input_recording_tests.cpp',
    'src/gui/ui_translation.hpp', 'src/gui/sequence_editor.hpp', 'src/gui/sequence_editor.cpp',
    'src/i18n/translations.cpp', 'src/i18n/ru/dictionary.cpp', 'src/i18n/en/dictionary.cpp',
    'src/storage/scenario_store.cpp', 'src/storage/scenario_store.hpp',
    'src/storage/scenario_codec.cpp', 'src/storage/scenario_codec.hpp',
    'src/platform/windows/input_support.hpp', 'tests/scenario_store_tests.cpp',
    'tests/input_actions_tests.cpp', 'tests/step_window_tests.cpp', 'tests/core_tests.cpp', 'tests/window_guard_tests.cpp', 'tests/sequence_tests.cpp',
    'tests/sequence_preview_tests.cpp', 'tests/sequence_conversion_tests.cpp',
    'include/clicknova/window_target.hpp', 'include/clicknova/start_delay.hpp', 'tests/start_delay_tests.cpp',
    'include/clicknova/duration_expression.hpp', 'tests/duration_expression_tests.cpp',
    'scripts/build-windows.ps1', 'scripts/run-windows.ps1', 'scripts/check-commit.ps1',
    'scripts/check-source-license.cmake', 'scripts/check-repository.cmake', 'scripts/package-source.ps1',
    'scripts/verify-source-tree.ps1'
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_) -PathType Leaf) })
if ($missing.Count -gt 0) {
    foreach ($item in $missing) { Write-Host "Missing: $item" -ForegroundColor Red }
    throw 'Incomplete ClickNova source tree. Extract the update ZIP directly into the existing ClickNova checkout.'
}
Write-Host 'ClickNova source tree: required files found.' -ForegroundColor Green
