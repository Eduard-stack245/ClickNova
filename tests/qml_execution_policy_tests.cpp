// SPDX-License-Identifier: MIT
#include "../src/gui/qml/execution_policy.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace clicknova::gui::qml;

int main() {
    std::size_t checks = 0;
    const auto expect = [&](bool condition) {
        ++checks;
        if (!condition) { std::cerr << "Execution policy assertion " << checks << " failed\n"; std::abort(); }
    };
    ExecutionObservation s;
    s.quickStartEnabled = s.quickStopEnabled = true;
    s.sequenceStartEnabled = s.sequenceStopEnabled = true;
    s.prepareRecorderEnabled = s.startRecorderEnabled = true;
    auto cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::Idle);
    expect(cap.canStartQuick && cap.canStartSequence && cap.canPrepareRecorder);
    expect(cap.canEditSteps && cap.canNavigate);
    expect(!cap.canStopQuick && !cap.canStopSequence && !cap.canStartRecorder);

    s.quickRunning = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::QuickRunning && cap.canStopQuick);
    expect(!cap.canStartQuick && !cap.canStartSequence && !cap.canPrepareRecorder && !cap.canEditSteps);
    s.quickRunning = false;
    s.sequenceRunning = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::SequenceRunning && cap.canStopSequence);
    expect(!cap.canStartQuick && !cap.canStartSequence && !cap.canPrepareRecorder);
    s.sequenceRunning = false;
    s.recorderOpen = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::RecorderPrepared && cap.canStartRecorder);
    expect(!cap.canStartQuick && !cap.canStartSequence && !cap.canEditSteps);
    s.recorderStarted = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::Recording && !cap.canNavigate);
    expect(!cap.canStartRecorder && !cap.canStartQuick && !cap.canStartSequence);
    s.recorderPaused = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::RecordingPaused && cap.canNavigate);
    expect(!cap.canEditSteps && !cap.canStartRecorder);
    s.quickRunning = true;
    cap = evaluateExecution(s);
    expect(cap.conflict && cap.phase == ExecutionPhase::Conflict);
    expect(cap.canStopQuick && !cap.canStartQuick && !cap.canStartSequence);

    s.recorderStarted = false;
    s.recorderPaused = false;
    s.recorderOpen = false;
    s.quickRunning = false;
    s.quickCleanupRequired = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::QuickRecoveryRequired);
    expect(cap.canStopQuick && !cap.canStartQuick && !cap.canStartSequence);
    expect(!cap.canPrepareRecorder && !cap.canStartRecorder && !cap.canNavigate);
    s.quickCleanupRequired = false;

    // A nested native element picker must lock mutations and new executions.
    // Native emergency Stop must remain independently reachable if a hotkey
    // started a session outside QML while the picker was open.
    s.nativePickerOpen = true;
    cap = evaluateExecution(s);
    expect(cap.phase == ExecutionPhase::NativePickerOpen);
    expect(!cap.canStartQuick && !cap.canStartSequence && !cap.canEditSteps);
    expect(!cap.canPrepareRecorder && !cap.canNavigate);
    s.nativePickerOpen = false;

    // All 2^12 combinations, including recovery and conflicting native
    // observations: the policy must never enable two competing starts while
    // active, and Stop must always be independently available if native Stop
    // is still enabled. Also check dispatch permissions and stable phase keys.
    for (unsigned bits = 0; bits < (1u << 12); ++bits) {
        ExecutionObservation o;
        o.quickRunning = bool(bits & 1);
        o.quickCleanupRequired = bool(bits & 2048);
        o.sequenceRunning = bool(bits & 2);
        o.recorderOpen = bool(bits & 4);
        o.recorderStarted = bool(bits & 8);
        o.recorderPaused = bool(bits & 16);
        o.quickStartEnabled = bool(bits & 32);
        o.quickStopEnabled = bool(bits & 64);
        o.sequenceStartEnabled = bool(bits & 128);
        o.sequenceStopEnabled = bool(bits & 256);
        o.prepareRecorderEnabled = bool(bits & 512);
        o.startRecorderEnabled = bool(bits & 1024);
        const auto p = evaluateExecution(o);
        const bool active = o.quickRunning || o.quickCleanupRequired ||
            o.sequenceRunning || o.recorderOpen || o.recorderStarted;
        expect(!active || (!p.canStartQuick && !p.canStartSequence && !p.canPrepareRecorder));
        expect(p.canStopQuick == ((o.quickRunning || o.quickCleanupRequired) && o.quickStopEnabled));
        expect(p.canStopSequence == (o.sequenceRunning && o.sequenceStopEnabled));
        expect(p.canStartRecorder == (o.recorderOpen && !o.recorderStarted &&
            !o.quickRunning && !o.quickCleanupRequired && !o.sequenceRunning &&
            !p.conflict && o.startRecorderEnabled));
        expect(p.allows(ExecutionCommand::StartQuick) == p.canStartQuick);
        expect(p.allows(ExecutionCommand::StopQuick) == p.canStopQuick);
        expect(p.allows(ExecutionCommand::StartSequence) == p.canStartSequence);
        expect(p.allows(ExecutionCommand::StopSequence) == p.canStopSequence);
        expect(p.allows(ExecutionCommand::PrepareRecorder) == p.canPrepareRecorder);
        expect(p.allows(ExecutionCommand::StartRecorder) == p.canStartRecorder);
        expect(p.allows(ExecutionCommand::EditSteps) == p.canEditSteps);
        expect(executionPhaseKey(p.phase).size() > 2);
        if (p.conflict) expect(!p.canStartQuick && !p.canStartSequence && !p.canStartRecorder);
    }
    std::cout << "Execution policy: " << checks << " assertions passed\n";
}
