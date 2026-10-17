// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QString>
namespace clicknova::portable {
// Initialize before constructing any window, QSettings or ScenarioStore.
// Attempts to keep all mutable files in <exe-folder>/data. If the folder is
// not writable, falls back to per-user storage without claiming portability.
bool initialize(QString* warning = nullptr);
QString dataRoot();
QString filePath(const QString& relativePath);
bool isPortable();
}
