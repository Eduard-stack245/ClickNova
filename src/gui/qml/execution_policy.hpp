// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

namespace clicknova::gui::qml {

// Presentation and command policy derived exclusively from the native state.
// It NEVER starts a worker, installs a hook, changes a hotkey or assumes that
// a click on Start succeeded. The existing backend is the source of truth.
enum class ExecutionPhase {
    Idle, QuickRunning, QuickRecoveryRequired, SequenceRunning, RecorderPrepared,
    Recording, RecordingPaused, NativePickerOpen, Conflict
};

enum class ExecutionCommand {
    StartQuick, StopQuick, StartSequence, StopSequence,
    PrepareRecorder, StartRecorder, EditSteps
};

struct ExecutionObservation {
    bool quickRunning{};
    bool quickCleanupRequired{};
    bool sequenceRunning{};
    bool recorderOpen{};
    bool recorderStarted{};
    bool recorderPaused{};
    bool nativePickerOpen{};
    bool quickStartEnabled{};
    bool quickStopEnabled{};
    bool sequenceStartEnabled{};
    bool sequenceStopEnabled{};
    bool prepareRecorderEnabled{};
    bool startRecorderEnabled{};
};

struct ExecutionCapabilities {
    ExecutionPhase phase{ExecutionPhase::Idle};
    bool canStartQuick{};
    bool canStopQuick{};
    bool canStartSequence{};
    bool canStopSequence{};
    bool canPrepareRecorder{};
    bool canStartRecorder{};
    bool canEditSteps{};
    bool canNavigate{};
    bool conflict{};

    bool allows(ExecutionCommand command) const noexcept;
};

ExecutionCapabilities evaluateExecution(const ExecutionObservation& source) noexcept;
std::string_view executionPhaseKey(ExecutionPhase phase) noexcept;

} // namespace clicknova::gui::qml
