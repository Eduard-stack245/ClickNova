// SPDX-License-Identifier: MIT
#include "../src/gui/qml/quick_configuration_model.hpp"
#include <QCoreApplication>
#include <QString>
#include <QVariantMap>
#include <cstdlib>
#include <iostream>

using clicknova::gui::qml::QuickConfigurationModel;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::size_t checks = 0;
    const auto expect = [&](bool ok, const char* meaning) {
        ++checks;
        if (!ok) { std::cerr << "Qt Quick config test failed: " << meaning << '\n'; std::abort(); }
    };
    QuickConfigurationModel model;
    expect(model.values().value(QStringLiteral("quickClickInterval")).toInt() == 200, "defaults");
    expect(!model.accepts(QStringLiteral("quickClickInterval"), QStringLiteral("9")), "interval min");
    expect(!model.accepts(QStringLiteral("quickClickInterval"), 10.5), "fractional");
    expect(!model.accepts(QStringLiteral("quickClickInterval"), true), "bool as int");
    expect(!model.setField(QStringLiteral("quickClickInterval"), QStringLiteral("9")), "reject invalid");
    expect(model.setField(QStringLiteral("quickClickInterval"), QStringLiteral("10")), "set minimum");
    expect(model.values().value(QStringLiteral("quickClickInterval")).toInt() == 10, "read minimum");
    expect(!model.setField(QStringLiteral("quickKeepAwake"), QStringLiteral("false")), "string as boolean");
    expect(model.setField(QStringLiteral("quickKeepAwake"), false), "set boolean");
    expect(!model.setField(QStringLiteral("quickStartAtTime"), QStringLiteral("24:00:00")), "invalid clock");
    expect(model.setField(QStringLiteral("quickStartAtTime"), QStringLiteral("23:59:59")), "valid clock");
    expect(!model.setField(QStringLiteral("destinationConfigured"), true), "internal field protected");
    expect(model.setField(QStringLiteral("quickDestinationX"), QStringLiteral("0")), "explicit zero destination");
    expect(model.values().value(QStringLiteral("destinationConfigured")).toBool(), "zero is configured");
    expect(model.engineOptions().value(QStringLiteral("destinationConfigured")).toBool(), "runtime destination");
    expect(model.setField(QStringLiteral("targetMode"), 1), "set target mode");
    expect(model.engineOptions().value(QStringLiteral("targetWindow")).toBool(), "runtime target bound");
    expect(!model.setField(QStringLiteral("targetHandle"), static_cast<qulonglong>(77)), "forbid arbitrary HWND");
    const QVariantMap previous = model.values();
    const QVariantMap invalidImport{{QStringLiteral("quickClickInterval"), 15},
                                   {QStringLiteral("quickMouseButton"), 5}};
    expect(!model.importNative(invalidImport), "invalid imported scenario");
    expect(model.values() == previous, "import is atomic");
    const QVariantMap import{{QStringLiteral("quickClickInterval"), 30},
                             {QStringLiteral("quickDestinationX"), 0},
                             {QStringLiteral("destinationConfigured"), false}};
    expect(model.importNative(import), "valid profile");
    expect(!model.values().value(QStringLiteral("destinationConfigured")).toBool(), "profile dest readiness");
    expect(model.setField(QStringLiteral("quickActionMode"), 3), "hold-until-stop mode");
    expect(model.setField(QStringLiteral("quickUnlimited"), true), "unlimited UI setting");
    expect(model.engineOptions().value(QStringLiteral("count")).toInt() == 1, "hold count = 1");
    expect(!model.engineOptions().value(QStringLiteral("unlimited")).toBool(), "hold may not auto-repeat");
    expect(model.engineOptions().size() == 20, "engine option set is fixed");
    std::cout << "Qt Quick configuration model: " << checks << " assertions passed\n";
    return 0;
}
