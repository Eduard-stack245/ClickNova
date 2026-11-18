// SPDX-License-Identifier: MIT
#include "execution_policy.hpp"

namespace clicknova::gui::qml {

ExecutionCapabilities evaluateExecution(const ExecutionObservation& s) noexcept {
    ExecutionCapabilities out;
    // Impossible combinations indicate an integration error. Fail closed for
    // every Start command, but leave active native Stop controls reachable.
    const bool malformedRecorder = (s.recorderStarted && !s.recorderOpen) ||
        (s.recorderPaused && !s.recorderStarted);
    const bool simultaneousWorkers = s.quickRunning && s.sequenceRunning;
    const bool cleanupConflict = s.quickCleanupRequired &&
        (s.sequenceRunning || s.recorderStarted);
    const bool captureConflict = s.recorderStarted &&
        (s.quickRunning || s.sequenceRunning);
    out.conflict = malformedRecorder || simultaneousWorkers || captureConflict || cleanupConflict;
    if (out.conflict) out.phase = ExecutionPhase::Conflict;
    else if (s.recorderStarted) out.phase = s.recorderPaused
        ? ExecutionPhase::RecordingPaused : ExecutionPhase::Recording;
    else if (s.quickRunning) out.phase = ExecutionPhase::QuickRunning;
    else if (s.quickCleanupRequired) out.phase = ExecutionPhase::QuickRecoveryRequired;
    else if (s.sequenceRunning) out.phase = ExecutionPhase::SequenceRunning;
    else if (s.recorderOpen) out.phase = ExecutionPhase::RecorderPrepared;
    else if (s.nativePickerOpen) out.phase = ExecutionPhase::NativePickerOpen;

    const bool idle = !out.conflict && !s.quickRunning && !s.quickCleanupRequired &&
        !s.sequenceRunning && !s.recorderOpen && !s.recorderStarted && !s.nativePickerOpen;
    out.canStartQuick = idle && s.quickStartEnabled;
    out.canStartSequence = idle && s.sequenceStartEnabled;
    out.canPrepareRecorder = idle && s.prepareRecorderEnabled;
    out.canStartRecorder = !out.conflict && s.recorderOpen &&
        !s.recorderStarted && !s.quickRunning && !s.quickCleanupRequired &&
        !s.sequenceRunning && !s.nativePickerOpen &&
        s.startRecorderEnabled;
    // Never block an emergency Stop merely because another component has
    // entered an inconsistent state. Native button state stays authoritative.
    out.canStopQuick = (s.quickRunning || s.quickCleanupRequired) && s.quickStopEnabled;
    out.canStopSequence = s.sequenceRunning && s.sequenceStopEnabled;
    out.canEditSteps = idle;
    out.canNavigate = (!s.recorderStarted || s.recorderPaused) &&
        !s.quickCleanupRequired && !s.nativePickerOpen;
    return out;
}

bool ExecutionCapabilities::allows(ExecutionCommand command) const noexcept {
    switch (command) {
    case ExecutionCommand::StartQuick: return canStartQuick;
    case ExecutionCommand::StopQuick: return canStopQuick;
    case ExecutionCommand::StartSequence: return canStartSequence;
    case ExecutionCommand::StopSequence: return canStopSequence;
    case ExecutionCommand::PrepareRecorder: return canPrepareRecorder;
    case ExecutionCommand::StartRecorder: return canStartRecorder;
    case ExecutionCommand::EditSteps: return canEditSteps;
    }
    return false;
}

std::string_view executionPhaseKey(ExecutionPhase phase) noexcept {
    switch (phase) {
    case ExecutionPhase::Idle: return "idle";
    case ExecutionPhase::QuickRunning: return "quick-running";
    case ExecutionPhase::QuickRecoveryRequired: return "quick-recovery-required";
    case ExecutionPhase::SequenceRunning: return "sequence-running";
    case ExecutionPhase::RecorderPrepared: return "recorder-prepared";
    case ExecutionPhase::Recording: return "recording";
    case ExecutionPhase::RecordingPaused: return "recording-paused";
    case ExecutionPhase::NativePickerOpen: return "native-picker-open";
    case ExecutionPhase::Conflict: return "conflict";
    }
    return "conflict";
}

} // namespace clicknova::gui::qml
