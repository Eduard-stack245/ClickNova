// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QHash>
#include <QString>

namespace clicknova::i18n {
const QHash<QString, QString>& russianDictionary();
QString englishText(const QString& text);
QString translate(const QString& english, const QString& language);
}
