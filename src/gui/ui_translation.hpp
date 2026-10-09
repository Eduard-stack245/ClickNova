// SPDX-License-Identifier: Apache-2.0
#pragma once

// UI-only English / Russian translations. Settings keys and native APIs remain English.
#include <QHash>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QString>

inline QString translateUiText(const QString& english, const QString& language) {
        if (language != QStringLiteral("ru")) return english;
        static const QHash<QString, QString> dictionary = {
            {"Auto Clicker", "Автокликер"}, {"Position", "Позиция"},
            {"Windows", "Окна"}, {"Profiles", "Профили"},
            {"Shortcuts", "Горячие клавиши"}, {"Settings", "Настройки"},
            {"Quick Auto Clicker", "Быстрый автокликер"},
            {"Configure clicks here. Enable optional position and window targeting independently below.",
             "Настройте клики. Вы можете независимо включать координаты и выбор окна."},
            {"Optional: set a fixed position when enabled on the Auto Clicker page. Otherwise the current cursor is used.",
             "Настройка фиксированной точки. Если функция выключена, используется текущий курсор."},
            {"Optional: select a window to restrict clicking. This is NOT background input; the target must stay foreground.",
             "Выберите окно для ограничения кликов. Это НЕ фоновый ввод: окно должно быть активным."},
            {"Save and restore your frequently used configurations.", "Сохраняйте и загружайте свои настройки."},
            {"Customize keyboard controls to avoid conflicts with other apps.",
             "Настройте сочетания клавиш, чтобы избежать конфликтов."},
            {"Choose the display language. Changes apply immediately.",
             "Выберите язык интерфейса. Изменения применяются сразу."},
            {"Language", "Язык"}, {"Interface language", "Язык интерфейса"},
            {"Mouse button", "Кнопка мыши"}, {"Left", "Левая"},
            {"Right", "Правая"}, {"Middle", "Средняя"},
            {"Interval", "Интервал"}, {"Mode", "Режим"},
            {"Unlimited clicks", "Бесконечные клики"}, {"Click count", "Количество кликов"},
            {"Cursor Position", "Позиция курсора"}, {"Position behavior", "Режим позиции"},
            {"Current cursor", "Текущий курсор"},
            {"Fixed screen coordinates", "Фиксированные координаты экрана"},
            {"Fixed client coordinates", "Координаты внутри окна"},
            {"Coordinates", "Координаты"}, {"Pick coordinates", "Выбор точки"},
            {"Capture cursor after 3 seconds", "Захватить курсор через 3 секунды"},
            {"Window Targeting", "Выбор целевого окна"}, {"Target behavior", "Режим окна"},
            {"Global mouse (all windows)", "Глобальная мышь (все окна)"},
            {"Bound window (foreground only)", "Выбранное окно (только активное)"},
            {"Window", "Окно"}, {"Select", "Выбор"},
            {"Refresh list", "Обновить список"},
            {"Pick active window after 3 seconds", "Захватить активное окно через 3 секунды"},
            {"Optional features", "Дополнительные функции"},
            {"The auto clicker always runs. Choose neither option, either option, or both. The Position and Windows pages only configure the options you enable here.",
             "Автокликер работает самостоятельно. Выберите любую из функций, обе или ни одной. Разделы «Позиция» и «Окна» настраивают включённые функции."},
            {"Use fixed click position", "Использовать фиксированную точку"},
            {"Configure position...", "Настроить позицию..."},
            {"Limit clicks to a selected window", "Ограничить клики выбранным окном"},
            {"Choose window...", "Выбрать окно..."},
            {"ACTIVE SETUP", "АКТИВНЫЕ НАСТРОЙКИ"},
            {"Save as...", "Сохранить как..."}, {"Load", "Загрузить"},
            {"Delete", "Удалить"}, {"Choose a saved profile", "Выберите профиль"},
            {"Profiles save click settings, coordinates, window preference, shortcuts and language. They are stored for your local user, never in Git. A window must be selected again after restarting.",
             "Профили сохраняют параметры кликов, координаты, выбор окна, сочетания клавиш и язык. Они находятся в личных настройках пользователя, вне Git. После перезапуска окно нужно выбрать заново."},
            {"Keyboard Controls", "Управление с клавиатуры"},
            {"Enable global Start shortcut", "Включить глобальную клавишу запуска"},
            {"Start enabled", "Запуск включён"},
            {"Start", "Старт"}, {"Stop", "Стоп"},
            {"Stop (required)", "Остановка (обязательно)"},
            {"Enable temporary point-picking shortcut", "Включить временную клавишу выбора точки"},
            {"Point picker", "Выбор точки"}, {"Pick point", "Выбрать точку"},
            {"Click a field and press a new key combination. Supported: F1–F11, or Ctrl/Alt + letter, number or F-key. Each shortcut must be unique. The Stop shortcut is required for safety. Start can be disabled; point picking always works with the 3-second button. Start is reserved only while idle; Stop is reserved only while running; the point shortcut is reserved only while picking. Any reserved shortcut is consumed by ClickNova, so pick a Stop key your automation will not need.",
             "Нажмите поле и задайте сочетание. Поддерживаются F1–F11 и Ctrl/Alt с буквой, цифрой или F-клавишей. Сочетания не должны совпадать. Остановка обязательна. Запуск можно отключить, а точку всегда можно выбрать через кнопку с задержкой. Клавиши перехватываются только в соответствующем режиме. Выбирайте клавишу остановки, не используемую вашим сценарием."},
            {"Ready", "Готово"}, {"Unlimited", "Без ограничений"},
            {"Current cursor position", "Текущая позиция курсора"},
            {"Any foreground window (global mouse)", "Любое активное окно (глобальная мышь)"},
            {"Selected window: NOT SELECTED", "Выбранное окно: НЕ ВЫБРАНО"},
            {"Position  · ON", "Позиция · ВКЛ"}, {"Position  · OFF", "Позиция · ВЫКЛ"},
            {"Windows  · ON", "Окна · ВКЛ"}, {"Windows  · OFF", "Окна · ВЫКЛ"},
            {"%1 clicks", "%1 кликов"},
            {"Fixed point in target window: (%1, %2)", "Точка в выбранном окне: (%1, %2)"},
            {"Fixed point on screen: (%1, %2)", "Точка на экране: (%1, %2)"},
            {"Selected window: %1", "Выбранное окно: %1"},
            {"%1 · Every %2 ms · %3\n%4\n%5", "%1 · каждые %2 мс · %3\n%4\n%5"},
            {"Select a window on the Windows page before pressing Start.", "Перед стартом выберите окно в разделе «Окна»."},
            {"Foreground only: keep the selected window focused. With Current cursor, the cursor must be inside its client area.",
             "Только активное окно. При текущем курсоре указатель должен находиться внутри окна."},
            {"No window restriction. Clicks use the global mouse and can affect other applications.",
             "Без привязки к окну. Клики используют глобальную мышь и могут затронуть другие приложения."},
            {"Global mode does not bind to a window.", "Глобальный режим не привязан к окну."},
            {"Select a window. Background clicks are not implemented; focus is required.",
             "Выберите окно. Фоновые клики пока не реализованы; требуется активное окно."},
            {"BOUND WINDOW: Uses the global mouse. Automation stops if the target loses focus or becomes covered.",
             "ВЫБРАННОЕ ОКНО: используется глобальная мышь. При потере фокуса или перекрытии окна автоматизация остановится."},
            {"GLOBAL MOUSE: Clicking affects the desktop; background automation is not available in this mode.",
             "ГЛОБАЛЬНАЯ МЫШЬ: клики затрагивают рабочий стол; фоновый режим недоступен."},
            {"Bound to: %1 (PID %2). Focus must remain on this window during clicks.",
             "Привязано к: %1 (PID %2). Во время кликов окно должно оставаться активным."},
            {"Saved window: %1. Please select it again before starting.",
             "Сохранённое окно: %1. Выберите его снова перед запуском."},
            {"Start · %1", "Старт · %1"}, {"Stop · %1", "Стоп · %1"},
            {"Capture in 3 seconds · or %1", "Захватить через 3 секунды · или %1"},
            {"Shortcut conflict or unsupported combination. Previous settings restored.",
             "Конфликт клавиш или неподдерживаемое сочетание. Предыдущие настройки восстановлены."},
            {"Keyboard shortcuts saved.", "Горячие клавиши сохранены."},
            {"Select a target window before starting.", "Выберите целевое окно перед запуском."},
            {"Stop shortcut is in use. Choose another shortcut before starting.",
             "Клавиша остановки занята. Выберите другое сочетание."},
            {"Starting in 3 seconds. Focus the selected window. %1 to stop.",
             "Запуск через 3 секунды. Перейдите в выбранное окно. Остановка: %1."},
            {"Starting in 3 seconds. Move the cursor to a safe position. %1 to stop.",
             "Запуск через 3 секунды. Переместите курсор в безопасное место. Остановка: %1."},
            {"Starting in %1 s · %2 to stop", "Запуск через %1 с · остановка: %2"},
            {"Running · %1 to stop", "Работает · остановка: %1"},
            {"Running · clicks: %1 · %2 to stop", "Работает · кликов: %1 · остановка: %2"},
            {"Stopped · clicks completed: %1", "Остановлено · выполнено кликов: %1"},
            {"Target window is not selected.", "Целевое окно не выбрано."},
            {"Move the cursor to the target. Capturing in 3 seconds%1.",
             "Наведите курсор на цель. Захват через 3 секунды%1."},
            {"Place the cursor inside the focused target window.",
             "Наведите курсор внутрь активного целевого окна."},
            {"Captured client position: %1, %2", "Сохранена точка в окне: %1, %2"},
            {"Captured screen position: %1, %2", "Сохранена точка на экране: %1, %2"},
            {"Unable to read the cursor position.", "Не удалось получить позицию курсора."},
            {"Focus your target window. Capturing in 3 seconds...", "Перейдите в нужное окно. Захват через 3 секунды..."},
            {"Could not capture a valid external foreground window.",
             "Не удалось захватить внешнее активное окно."},
            {"Window selected. Clicks require it to stay foreground.",
             "Окно выбрано. Для кликов оно должно оставаться активным."},
            {"Save profile", "Сохранить профиль"}, {"Profile name:", "Имя профиля:"},
            {"Invalid name", "Недопустимое имя"},
            {"Use 1-40 letters, numbers, spaces, hyphens, or underscores. Start with a letter or number.",
             "Введите 1–40 букв, цифр, пробелов, дефисов или подчёркиваний. Первый символ — буква или цифра."},
            {"Overwrite profile", "Перезаписать профиль"},
            {"Replace the existing profile '%1'?", "Заменить существующий профиль «%1»?"},
            {"Saved profile: %1", "Профиль сохранён: %1"},
            {"Loaded profile: %1", "Профиль загружен: %1"},
            {"Delete profile", "Удалить профиль"},
            {"Delete saved profile '%1'?", "Удалить сохранённый профиль «%1»?"},
            {"Deleted profile: %1", "Профиль удалён: %1"},
            {"Profile shortcut unavailable. No settings were changed.",
             "Сочетание из профиля недоступно. Настройки не изменены."},
            {"Linux input injection is not available in this prototype.",
             "В этой версии ввод в Linux пока недоступен."},
            {"Start shortcut unavailable. Choose another key in Shortcuts.",
             "Клавиша запуска недоступна. Выберите другую в разделе «Горячие клавиши»."},
            {"Automation stopped.", "Автоматизация остановлена."},
            {"Target window is ready", "Целевое окно готово"},
            {"Window targeting is not implemented on this platform", "Привязка к окну недоступна на этой платформе"},
            {"Target window closed or its handle changed", "Целевое окно закрыто или изменилось"},
            {"Target window is hidden", "Целевое окно скрыто"},
            {"Target window is minimized", "Целевое окно свёрнуто"},
            {"Target window lost focus; automation stopped", "Целевое окно потеряло фокус; автоматизация остановлена"},
            {"Click location is outside the target client area", "Точка клика вне клиентской области окна"},
            {"Target click location is covered by another window", "Точка клика закрыта другим окном"},
            {"Unknown window targeting error", "Неизвестная ошибка выбора окна"},
            {"Select a running window", "Выберите запущенное окно"},
            {"Selected window", "Выбранное окно"},
            {"Any foreground window", "Любое активное окно"},
            {" or press %1", " или нажмите %1"},
            {"Windows could not move the cursor to the selected position.",
             "Windows не удалось переместить курсор в указанную точку."},
            {"Windows rejected the input event. Automation has stopped.",
             "Windows отклонила событие ввода. Автоматизация остановлена."}
        };
        const auto it = dictionary.constFind(english);
        if (it != dictionary.constEnd()) return *it;
        if (english.endsWith(QStringLiteral(" Automation stopped."))) {
            const QString prefix = english.left(english.size() - QStringLiteral(" Automation stopped.").size());
            return translateUiText(prefix, language) + QStringLiteral(" ") +
                   dictionary.value(QStringLiteral("Automation stopped."));
        }
        // Resolve formatted messages while keeping user-provided arguments intact.
        static const auto patterns = [] {
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
