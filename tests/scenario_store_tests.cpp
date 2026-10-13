// SPDX-License-Identifier: Apache-2.0
#include "../src/storage/scenario_store.hpp"
#include "../src/storage/scenario_codec.hpp"
#include "../src/i18n/translations.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QVariantList>
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    clicknova::storage::ScenarioStore store(temp.path());
    const auto single = clicknova::storage::Kind::AutoClicker;
    const auto sequence = clicknova::storage::Kind::Sequence;

    clicknova::storage::AutoClickerScenario autoScenario;
    autoScenario.button = 4;
    autoScenario.holdDurationMs = 275;
    autoScenario.actionMode = 2;
    autoScenario.destinationX = 700;
    autoScenario.destinationY = 400;
    autoScenario.movementMs = 850;
    autoScenario.destinationConfigured = true;
    autoScenario.count = 50;
    autoScenario.startDelayValue = 12;
    autoScenario.startDelayUnit = 2;
    autoScenario.startMode = 1;
    autoScenario.startTimeSeconds = 18 * 3600 + 30 * 60 + 15;
    autoScenario.unlimited = false;
    autoScenario.screen = QPoint(-15, 42);
    autoScenario.windowTitle = QString::fromUtf8("Окно редактора");

    clicknova::storage::SequenceScenario sequenceScenario;
    sequenceScenario.repeatCount = 3;
    sequenceScenario.passDelayMs = 750;
    clicknova::sequence::Step step;
    step.x = 123; step.y = 87;
    step.clickCount = 5;
    step.intervalMs = 150;
    step.space = clicknova::sequence::Space::Client;
    step.ownWindow = true;
    step.windowTitle = "Target window";
    step.windowHandle = 999; // Must not be persisted.
    step.processId = 1234;
    sequenceScenario.steps.push_back(step);

    QString error;
    const auto autoValues = clicknova::storage::encodeScenario(autoScenario);
    const auto sequenceValues = clicknova::storage::encodeScenario(sequenceScenario);
    if (!store.save(single, "Work", autoValues, &error)) return 2;
    if (!store.save(sequence, "Work", sequenceValues, &error)) return 3;
    if (store.names(single) != QStringList{"Work"} || store.names(sequence) != QStringList{"Work"}) return 4;
    const auto loadedAuto = store.load(single, "Work");
    const auto loadedSequence = store.load(sequence, "Work");
    if (!loadedAuto || !loadedSequence) return 5;

    clicknova::storage::AutoClickerScenario restoredAuto;
    clicknova::storage::SequenceScenario restoredSequence;
    if (!clicknova::storage::decodeScenario(*loadedAuto, restoredAuto, &error) ||
        !clicknova::storage::decodeScenario(*loadedSequence, restoredSequence, &error)) return 6;
    if (restoredAuto.count != 50 || restoredAuto.startDelayValue != 12 ||
        restoredAuto.startDelayUnit != 2 || restoredAuto.startMode != 1 ||
        restoredAuto.startTimeSeconds != 18 * 3600 + 30 * 60 + 15 ||
        restoredAuto.button != 4 ||
        restoredAuto.holdDurationMs != 275 || restoredAuto.actionMode != 2 ||
        restoredAuto.destinationX != 700 || restoredAuto.destinationY != 400 ||
        restoredAuto.movementMs != 850 || !restoredAuto.destinationConfigured ||
        restoredAuto.screen.x() != -15 ||
        restoredAuto.windowTitle != autoScenario.windowTitle ||
        restoredSequence.repeatCount != 3 || restoredSequence.passDelayMs != 750 ||
        restoredSequence.steps.size() != 1 || restoredSequence.steps[0].clickCount != 5 ||
        restoredSequence.steps[0].intervalMs != 150 || !restoredSequence.steps[0].ownWindow ||
        restoredSequence.steps[0].windowHandle != 0 || restoredSequence.steps[0].processId != 0) return 7;

    // Old saved scenarios do not contain movement fields and must load as clicks.
    auto legacyAuto = autoValues;
    for (const auto& key : {"actionMode", "destinationX", "destinationY",
                             "movementMs", "destinationConfigured", "startDelayValue", "startDelayUnit",
                             "startMode", "startTimeSeconds"})
        legacyAuto.remove(QString::fromLatin1(key));
    clicknova::storage::AutoClickerScenario legacy;
    if (!clicknova::storage::decodeScenario(legacyAuto, legacy) ||
        legacy.actionMode != 0 || legacy.destinationConfigured ||
        legacy.startDelayValue != 3 || legacy.startDelayUnit != 0 ||
        legacy.startMode != 0 || legacy.startTimeSeconds != 18 * 3600 + 30 * 60) return 17;
    auto corruptAuto = autoValues;
    corruptAuto[QStringLiteral("startDelayUnit")] = 8;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 18;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("startDelayValue")] = -1;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 19;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("startMode")] = 2;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 20;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("startTimeSeconds")] = 86400;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 21;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("startTimeSeconds")] = -1;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 22;

    // Incomplete or malicious settings never alter the existing destination.
    const auto untouchedCount = restoredAuto.count;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("count")] = 1.5;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto, &error) ||
        restoredAuto.count != untouchedCount) return 8;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("unlimited")] = QStringLiteral("false");
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 9;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("screenX")] = 200000;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 10;
    const auto untouchedSteps = restoredSequence.steps.size();
    auto corruptSequence = sequenceValues;
    auto records = corruptSequence[QStringLiteral("steps")].toList();
    auto action = records[0].toMap();
    action[QStringLiteral("clickCount")] = 2147483648LL;
    records[0] = action;
    corruptSequence[QStringLiteral("steps")] = records;
    if (clicknova::storage::decodeScenario(corruptSequence, restoredSequence) ||
        restoredSequence.steps.size() != untouchedSteps) return 11;
    corruptSequence = sequenceValues;
    records = corruptSequence[QStringLiteral("steps")].toList();
    action = records[0].toMap();
    action[QStringLiteral("space")] = 0; // Window-bound click cannot use screen space.
    records[0] = action;
    corruptSequence[QStringLiteral("steps")] = records;
    if (clicknova::storage::decodeScenario(corruptSequence, restoredSequence)) return 12;

    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("actionMode")] = 99;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 18;
    corruptAuto = autoValues;
    corruptAuto[QStringLiteral("movementMs")] = -1;
    if (clicknova::storage::decodeScenario(corruptAuto, restoredAuto)) return 19;
    // Scenario names are isolated by type and cannot escape their data directory.
    if (store.save(single, "../invalid", autoValues) ||
        store.save(single, ".", autoValues)) return 13;
    if (!store.remove(single, "Work") || !store.names(single).isEmpty() ||
        store.names(sequence) != QStringList{"Work"}) return 14;
    if (store.load(single, "Missing").has_value()) return 15;
    if (clicknova::i18n::translate("Save scenario", "ru") !=
            QString::fromUtf8("Сохранить сценарий") ||
        clicknova::i18n::translate("Save scenario", "en") != "Save scenario") return 16;
    std::cout << "Scenario storage, codec and translations tests passed\n";
    return 0;
}
