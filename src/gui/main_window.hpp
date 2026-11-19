// SPDX-License-Identifier: MIT
#pragma once
class QMainWindow;
#include <QVariantMap>
QMainWindow* createClickNovaMainWindow();
namespace clicknova::gui { class QuickClickerService; }
// Typed service accessor; no QObject::findChild or hidden button clicks.
clicknova::gui::QuickClickerService* clicknovaQuickClicker(QMainWindow* window);
bool clicknovaStartQuick(QMainWindow* window);
bool clicknovaStopQuick(QMainWindow* window);
// QML input is a typed, validated options map; HWND identity stays native.
bool clicknovaStartQuickWithOptions(QMainWindow* window, const QVariantMap& options);
bool clicknovaQuickDestinationReady(const QMainWindow* window);
bool clicknovaQuickMarkDestination(QMainWindow* window);
bool clicknovaShortcutSmoke();
