// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../i18n/translations.hpp"

inline QString translateUiText(const QString& text, const QString& language) {
    return clicknova::i18n::translate(text, language);
}
