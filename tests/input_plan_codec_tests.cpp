// SPDX-License-Identifier: Apache-2.0
#include "../src/storage/input_plan_codec.hpp"
#include "../src/storage/scenario_store.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

using namespace clicknova;
using namespace clicknova::input;
using namespace clicknova::storage;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    assert(tmp.isValid());
    ScenarioStore store(tmp.path());
    const Plan original{{KeyDown{0x11}, Click{Button::X1, 2, 75},
                         Wheel{Axis::Horizontal, -120}, KeyUp{0x11}, Wait{250},
                         MouseHold{Button::X2, 350}, KeyHold{0x70, 500}, TypeText{u"Привет, world! 😀"},
                         MovePointer{MoveMode::Relative, -120, 75, 500},
                         MovePointer{MoveMode::Absolute, 900, 640, 0},
                         MovePointer{MoveMode::WindowClient, 25, 80, 0}}, 3, 120};
    assert(validate(original).empty());
    QString error;
    assert(store.save(Kind::InputLab, QStringLiteral("Mixed test"), encodeInputPlan(original), &error));
    assert(store.names(Kind::InputLab) == QStringList{QStringLiteral("Mixed test")});
    assert(store.names(Kind::Sequence).isEmpty());
    const auto record = store.load(Kind::InputLab, QStringLiteral("Mixed test"), &error);
    assert(record);
    const auto roundtrip = decodeInputPlan(*record, &error);
    assert(roundtrip && roundtrip->actions.size() == original.actions.size());
    assert(roundtrip->repetitions == 3 && roundtrip->passDelayMs == 120);
    assert(std::get<Click>(roundtrip->actions[1]).button == Button::X1);
    assert(std::get<Click>(roundtrip->actions[1]).count == 2);
    assert(std::get<Wheel>(roundtrip->actions[2]).delta == -120);
    assert(std::get<MouseHold>(roundtrip->actions[5]).button == Button::X2);
    assert(std::get<MouseHold>(roundtrip->actions[5]).durationMs == 350);
    assert(std::get<KeyHold>(roundtrip->actions[6]).durationMs == 500);
    assert(std::get<TypeText>(roundtrip->actions[7]).text == u"Привет, world! 😀");
    assert(std::get<MovePointer>(roundtrip->actions[8]).x == -120);
    assert(std::get<MovePointer>(roundtrip->actions[8]).durationMs == 500);
    assert(std::get<MovePointer>(roundtrip->actions[9]).mode == MoveMode::Absolute);
    assert(std::get<MovePointer>(roundtrip->actions[10]).mode == MoveMode::WindowClient);
    assert(std::get<MovePointer>(roundtrip->actions[10]).x == 25);
    // Editor clipboard must support every action, even a standalone Down/Up
    // (which is not a valid complete playback plan by itself).
    for (const auto& step : original.actions) {
        auto restored = decodeInputAction(encodeInputAction(step));
        assert(restored && restored->index() == step.index());
    }
    assert(decodeInputAction(encodeInputAction(ButtonDown{Button::X2})).has_value());
    const RunScript script{u"Write-Output 'Привет'"};
    const auto serializedScript = encodeInputAction(script);
    assert(serializedScript.value(QStringLiteral("type")).toString() == QStringLiteral("powershell"));
    const auto parsedScript = decodeInputAction(serializedScript);
    assert(parsedScript && std::get<RunScript>(*parsedScript).code == script.code);
    const Plan scriptPlan{{script, Wait{0}}, 1, 0};
    const auto scriptRoundtrip = decodeInputPlan(encodeInputPlan(scriptPlan));
    assert(scriptRoundtrip && scriptRoundtrip->actions.size() == 2);
    assert(std::get<RunScript>(scriptRoundtrip->actions[0]).code == script.code);
    // Each action saves a portable window identity, never HWND/PID.
    const StepWindow firstWindow{"Browser - Search", "browser.exe", true, 45'000};
    const StepWindow secondWindow{"Editor - Document", "editor.exe", false, 0};
    const Plan perWindow{{KeyTap{65}, Wait{0}, TypeText{u"text"}}, 1, 0, false,
        {firstWindow, {}, secondWindow}};
    const auto perWindowData = encodeInputPlan(perWindow);
    const auto perWindowRoundtrip = decodeInputPlan(perWindowData);
    assert(perWindowRoundtrip && perWindowRoundtrip->stepWindows == perWindow.stepWindows);
    assert(!perWindowData.contains(QStringLiteral("HWND")) && !perWindowData.contains(QStringLiteral("PID")));
    const auto encodedAction = encodeBoundInputAction(KeyTap{65}, firstWindow);
    const auto decodedBinding = decodeStepWindow(encodedAction);
    assert(decodedBinding && *decodedBinding == firstWindow);
    // Informational window provenance survives autosave and JSON profiles.
    StepWindow observed{"Workspace 13", "explorer.exe"};
    observed.enforce = false;
    const auto observedJson = encodeBoundInputAction(Wait{3000}, observed);
    assert(observedJson.value(QStringLiteral("enforceTargetWindow")).isBool());
    const auto observedCopy = decodeStepWindow(observedJson);
    assert(observedCopy && *observedCopy == observed);
    const Plan observedPlan{{Wait{3000}, MovePointer{MoveMode::Absolute, 5, 5, 0}},
        1, 0, false, {observed, observed}};
    const auto restoredObserved = decodeInputPlan(encodeInputPlan(observedPlan));
    assert(restoredObserved && restoredObserved->stepWindows == observedPlan.stepWindows);
    // Existing JSON plans had no enforcement flag and must remain strict.
    auto legacyRequired = encodedAction;
    legacyRequired.remove(QStringLiteral("enforceTargetWindow"));
    const auto legacyBinding = decodeStepWindow(legacyRequired);
    assert(legacyBinding && legacyBinding->enforce);
    auto corruptBinding = encodedAction;
    corruptBinding[QStringLiteral("waitForWindowMs")] = QStringLiteral("-1");
    assert(!decodeStepWindow(corruptBinding));
    corruptBinding = encodedAction;
    corruptBinding[QStringLiteral("enforceTargetWindow")] = QStringLiteral("false");
    assert(!decodeStepWindow(corruptBinding));
    const auto oldFormat = decodeInputPlan(encodeInputPlan(original));
    assert(oldFormat && oldFormat->stepWindows.size() == original.actions.size());
    for (const auto& binding : oldFormat->stepWindows) assert(binding.title.empty());
    QJsonObject invalidSingle = encodeInputAction(KeyTap{65});
    invalidSingle.insert(QStringLiteral("vk"), -17);
    assert(!decodeInputAction(invalidSingle));
    // Save the requirement, but never serialize a native window handle.
    const Plan guarded{{MovePointer{MoveMode::WindowClient, 25, 80, 0}, Click{}}, 1, 0, true};
    const auto guardedData = encodeInputPlan(guarded);
    assert(guardedData.value(QStringLiteral("requireTargetWindow")).toBool());
    const auto guardedRestore = decodeInputPlan(guardedData);
    assert(guardedRestore && guardedRestore->requireTargetWindow);
    assert(!guardedData.contains(QStringLiteral("handle")));
    QVariantMap legacyData = guardedData;
    legacyData.remove(QStringLiteral("requireTargetWindow"));
    const auto legacyRestore = decodeInputPlan(legacyData);
    assert(legacyRestore && !legacyRestore->requireTargetWindow);
    QVariantMap bad = *record;
    auto invalidActions = bad[QStringLiteral("actions")].toList();
    auto invalidText = invalidActions[7].toMap();
    invalidText[QStringLiteral("text")] = QString();
    invalidActions[7] = invalidText;
    bad[QStringLiteral("actions")] = invalidActions;
    assert(!decodeInputPlan(bad));

    bad = *record;
    bad[QStringLiteral("repetitions")] = 2.5;
    assert(!decodeInputPlan(bad));
    bad = *record;
    bad[QStringLiteral("planVersion")] = 999;
    assert(!decodeInputPlan(bad));
    bad = *record;
    auto list = bad[QStringLiteral("actions")].toList();
    auto click = list[1].toMap();
    click[QStringLiteral("button")] = 9;
    list[1] = click;
    bad[QStringLiteral("actions")] = list;
    assert(!decodeInputPlan(bad));
    bad = *record;
    list = bad[QStringLiteral("actions")].toList();
    auto key = list[0].toMap();
    key[QStringLiteral("type")] = QStringLiteral("unknownFutureType");
    list[0] = key;
    bad[QStringLiteral("actions")] = list;
    assert(!decodeInputPlan(bad));
    bad = *record;
    list = bad[QStringLiteral("actions")].toList();
    auto brokenMove = list.last().toMap();
    brokenMove[QStringLiteral("durationMs")] = -1;
    list[list.size() - 1] = brokenMove;
    bad[QStringLiteral("actions")] = list;
    assert(!decodeInputPlan(bad));
    bad = *record;
    list = bad[QStringLiteral("actions")].toList();
    // Remove Ctrl release (index 3), leaving Ctrl held.
    list.removeAt(3);
    bad[QStringLiteral("actions")] = list;
    assert(!decodeInputPlan(bad));
    bad = *record;
    bad[QStringLiteral("passDelayMs")] = QStringLiteral("9223372036854775808");
    assert(!decodeInputPlan(bad));
    const Plan atClock{{Click{Button::Left, 1, 50}, WaitUntil{18, 30, 15}, KeyTap{0x41}}};
    assert(validate(atClock).empty());
    const auto restoredClock = decodeInputPlan(encodeInputPlan(atClock));
    assert(restoredClock && restoredClock->actions.size() == 3);
    const auto clockStep = std::get<WaitUntil>(restoredClock->actions[1]);
    assert(clockStep.hour == 18 && clockStep.minute == 30 && clockStep.second == 15);
    auto corruptedClock = encodeInputPlan(atClock);
    auto clockRows = corruptedClock[QStringLiteral("actions")].toList();
    auto clockRecord = clockRows[1].toMap();
    clockRecord[QStringLiteral("hour")] = 24;
    clockRows[1] = clockRecord;
    corruptedClock[QStringLiteral("actions")] = clockRows;
    assert(!decodeInputPlan(corruptedClock));
        const Plan wide{{Click{Button::Left, 8'000'000'000LL, 10},
                     Wait{14LL * 24 * 60 * 60 * 1000}}, 3,
                    5LL * 24 * 60 * 60 * 1000};
    assert(validate(wide).empty());
    const auto wideRoundtrip = decodeInputPlan(encodeInputPlan(wide));
    assert(wideRoundtrip);
    assert(std::get<Click>(wideRoundtrip->actions.front()).count == 8'000'000'000LL);
    assert(std::get<Wait>(wideRoundtrip->actions.back()).durationMs == 1'209'600'000LL);
    assert(wideRoundtrip->passDelayMs == 432'000'000LL);
    assert(store.load(Kind::Sequence, QStringLiteral("Mixed test")) == std::nullopt);
    assert(store.remove(Kind::InputLab, QStringLiteral("Mixed test")));
    assert(store.names(Kind::InputLab).isEmpty());
}
