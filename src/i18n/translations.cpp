// SPDX-License-Identifier: Apache-2.0
#include "translations.hpp"
#include <QList>
#include <QPair>
#include <QRegularExpression>

namespace clicknova::i18n {
QString translate(const QString& english, const QString& language) {
    if (language != QStringLiteral("ru")) return englishText(english);
    const auto& dictionary = russianDictionary();
        const auto it = dictionary.constFind(english);
        if (it != dictionary.constEnd()) return *it;
        if (english.endsWith(QStringLiteral(" Automation stopped."))) {
            const QString prefix = english.left(english.size() - QStringLiteral(" Automation stopped.").size());
            return translate(prefix, language) + QStringLiteral(" ") +
                   dictionary.value(QStringLiteral("Automation stopped."));
        }
        // Resolve formatted messages while keeping user-provided arguments intact.
        static const auto patterns = [&dictionary] {
            QList<QPair<QRegularExpression, QString>> result;
            for (auto iter = dictionary.constBegin(); iter != dictionary.constEnd(); ++iter) {
                if (!iter.key().contains(QStringLiteral("%1"))) continue;
                QString pattern = QRegularExpression::escape(iter.key());
                pattern.replace(QStringLiteral("%1"), QStringLiteral("(.*?)"));
                pattern.replace(QStringLiteral("%2"), QStringLiteral("(.*?)"));
                result.append({QRegularExpression(QStringLiteral("^") + pattern + QStringLiteral("$")), *iter});
            }
            return result;
        }();
        for (const auto& candidate : patterns) {
            const auto match = candidate.first.match(english);
            if (!match.hasMatch()) continue;
            QString translated = candidate.second;
            for (int index = match.lastCapturedIndex(); index >= 1; --index)
                translated.replace(QStringLiteral("%") + QString::number(index), match.captured(index));
            return translated;
        }
        return english;
}
} // namespace clicknova::i18n
