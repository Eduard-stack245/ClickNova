// SPDX-License-Identifier: MIT
#include "../src/gui/qml/accessibility_inspector.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QString>
#include <iostream>
using namespace clicknova;
using clicknova::gui::qml::describeAccessibility;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto empty = describeAccessibility(nullptr, nullptr, false);
    if (empty.value("stateKey").toString() != QStringLiteral("none") || empty.value("canSearch").toBool()) return 1;
    input::Action action = input::Click{input::Button::Left, 1, 100};
    const auto noSelector = describeAccessibility(&action, nullptr, false);
    if (noSelector.value("stateKey").toString() != QStringLiteral("not-configured") ||
        !noSelector.value("canSearch").toBool() || noSelector.value("canClear").toBool()) return 2;
    input::StepWindow binding;
    binding.elementAutomationId="BANK_PASSWORD_SECRET";
    binding.elementName="SECRET_SELECTOR_491";
    binding.elementClass="PRIVATE_UTILITY_TOOL";
    binding.elementTab="https://private.example/secret";
    binding.elementValue="password-super-secret";
    binding.title="Bank account details";
    binding.process="privatebank.exe";
    binding.enforce=true;
    binding.elementBrowser=true;
    for (int p=0;p<=4;++p) {
        binding.elementProvider=p;
        const auto desc = describeAccessibility(&action,&binding,false);
        if (desc.value("stateKey").toString()!=QStringLiteral("configured-unverified") ||
            !desc.value("canClear").toBool() || !desc.value("canSearch").toBool() ||
            !desc.value("hasRequiredWindow").toBool() ||
            !desc.value("hasBrowserScope").toBool() || desc.value("verified").toBool()) return 3;
        const auto content=QJsonDocument::fromVariant(desc).toJson();
        for (const char* secret: {"BANK_PASSWORD_SECRET", "SECRET_SELECTOR_491", "PRIVATE_UTILITY_TOOL",
             "private.example", "password-super-secret", "Bank account details", "privatebank.exe"})
            if (content.contains(secret)) return 4;
        const auto uk=describeAccessibility(&action,&binding,true);
        if (uk.value("stateKey")!=desc.value("stateKey") || uk.value("providerLabel").toString().isEmpty()) return 5;
    }
    binding.elementProvider=99;
    if (describeAccessibility(&action,&binding,false).value("canSearch").toBool()) return 6;
    if (!describeAccessibility(&action,&binding,false).value("canClear").toBool()) return 7;
    action=input::Click{input::Button::Right,1,100};
    if (describeAccessibility(&action,&binding,false).value("canSearch").toBool()) return 8;
    action=input::ElementTask{};
    binding.elementProvider=2;
    if (!describeAccessibility(&action,&binding,false).value("canSearch").toBool()) return 9;
    std::cout << "PASS Qt accessibility inspector privacy and provider checks\n";
    return 0;
}
