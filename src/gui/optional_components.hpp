// SPDX-License-Identifier: Apache-2.0
#pragma once
class QWidget;
namespace clicknova::gui::components {
// First run: ask once per portable data folder. Never start a download by default.
void showFirstRun(QWidget* parent);
// Settings: optional components can be installed later or rechecked at any time.
QWidget* createSettingsPanel(QWidget* parent = nullptr);
}
