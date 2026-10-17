// SPDX-License-Identifier: Apache-2.0
#include "input_plan_codec.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

namespace clicknova::storage {
namespace {
void fail(QString* error, const QString& message) { if (error) *error = message; }

bool number(const QJsonObject& obj, const char* field, int min, int max, int& result) {
    const auto value = obj.value(QLatin1String(field));
    if (!value.isDouble()) return false;
    const double n = value.toDouble();
    if (!std::isfinite(n) || n < min || n > max || std::trunc(n) != n) return false;
    result = static_cast<int>(n);
    return true;
}

bool number64(const QJsonObject& obj, const char* field, std::int64_t minimum,
              std::int64_t& result) {
    const auto value = obj.value(QLatin1String(field));
    if (value.isString()) {
        bool ok = false;
        const auto parsed = value.toString().toLongLong(&ok);
        if (!ok || parsed < minimum) return false;
        result = parsed;
        return true;
    }
    if (!value.isDouble()) return false;
    const double n = value.toDouble();
    // Numbers from older profiles are always exactly representable here;
    // new wide counters use decimal strings, never lossy JSON doubles.
    if (!std::isfinite(n) || n < static_cast<double>(minimum) ||
        n > 9007199254740991.0 || std::trunc(n) != n) return false;
    result = static_cast<std::int64_t>(n);
    return true;
}

QJsonObject writeAction(const input::Action& action) {
    return std::visit([](const auto& a) -> QJsonObject {
        using T = std::decay_t<decltype(a)>;
        QJsonObject obj;
        if constexpr (std::is_same_v<T, input::Click>) {
            obj[QStringLiteral("type")] = QStringLiteral("click");
            obj[QStringLiteral("button")] = static_cast<int>(a.button);
            obj[QStringLiteral("count")] = QString::number(a.count);
            obj[QStringLiteral("intervalMs")] = a.intervalMs;
        } else if constexpr (std::is_same_v<T, input::ButtonDown> || std::is_same_v<T, input::ButtonUp>) {
            obj[QStringLiteral("type")] = std::is_same_v<T, input::ButtonDown> ? QStringLiteral("mouseDown") : QStringLiteral("mouseUp");
            obj[QStringLiteral("button")] = static_cast<int>(a.button);
        } else if constexpr (std::is_same_v<T, input::KeyTap> ||
                             std::is_same_v<T, input::KeyDown> || std::is_same_v<T, input::KeyUp>) {
            obj[QStringLiteral("type")] = std::is_same_v<T, input::KeyTap> ? QStringLiteral("keyTap") :
                std::is_same_v<T, input::KeyDown> ? QStringLiteral("keyDown") : QStringLiteral("keyUp");
            obj[QStringLiteral("vk")] = a.virtualKey;
        } else if constexpr (std::is_same_v<T, input::MouseHold>) {
            obj[QStringLiteral("type")] = QStringLiteral("mouseHold");
            obj[QStringLiteral("button")] = static_cast<int>(a.button);
            obj[QStringLiteral("durationMs")] = a.durationMs;
        } else if constexpr (std::is_same_v<T, input::KeyHold>) {
            obj[QStringLiteral("type")] = QStringLiteral("keyHold");
            obj[QStringLiteral("vk")] = a.virtualKey;
            obj[QStringLiteral("durationMs")] = a.durationMs;
        } else if constexpr (std::is_same_v<T, input::MovePointer>) {
            obj[QStringLiteral("type")] = QStringLiteral("movePointer");
            obj[QStringLiteral("mode")] = static_cast<int>(a.mode);
            obj[QStringLiteral("x")] = a.x;
            obj[QStringLiteral("y")] = a.y;
            obj[QStringLiteral("durationMs")] = a.durationMs;
        } else if constexpr (std::is_same_v<T, input::RunScript>) {
            obj[QStringLiteral("type")] = QStringLiteral("powershell");
            obj[QStringLiteral("code")] = QString::fromStdU16String(a.code);
        } else if constexpr (std::is_same_v<T, input::WaitUntil>) {
            obj[QStringLiteral("type")] = QStringLiteral("waitUntil");
            obj[QStringLiteral("hour")] = a.hour;
            obj[QStringLiteral("minute")] = a.minute;
            obj[QStringLiteral("second")] = a.second;
        } else if constexpr (std::is_same_v<T, input::ElementTask>) {
            obj[QStringLiteral("type")] = QStringLiteral("elementTask");
            obj[QStringLiteral("command")] = static_cast<int>(a.command);
            obj[QStringLiteral("text")] = QString::fromStdU16String(a.text);
            obj[QStringLiteral("axis")] = static_cast<int>(a.axis);
            obj[QStringLiteral("delta")] = a.delta;
            obj[QStringLiteral("timeoutMs")] = a.timeoutMs;
            obj[QStringLiteral("pollMs")] = a.pollMs;
            obj[QStringLiteral("doubleIntervalMs")] = a.doubleIntervalMs;
        } else if constexpr (std::is_same_v<T, input::TypeText>) {
            obj[QStringLiteral("type")] = QStringLiteral("text");
            obj[QStringLiteral("text")] = QString::fromStdU16String(a.text);
        } else if constexpr (std::is_same_v<T, input::Wheel>) {
            obj[QStringLiteral("type")] = QStringLiteral("wheel");
            obj[QStringLiteral("axis")] = static_cast<int>(a.axis);
            obj[QStringLiteral("delta")] = a.delta;
        } else {
            obj[QStringLiteral("type")] = QStringLiteral("wait");
            obj[QStringLiteral("durationMs")] = QString::number(a.durationMs);
        }
        return obj;
    }, action);
}

std::optional<input::Action> readAction(const QJsonObject& obj) {
    const auto type = obj.value(QStringLiteral("type"));
    if (!type.isString()) return {};
    const auto name = type.toString();
    int value = 0;
    if (name == QStringLiteral("elementTask")) {
        int command = 0, axis = 0, delta = 0, timeout = 0, poll = 0, doubleInterval = 100;
        const auto text = obj.value(QStringLiteral("text"));
        if (!text.isString() ||
            !number(obj, "command", 0, 5, command) ||
            !number(obj, "axis", 0, 1, axis) ||
            !number(obj, "delta", -1200, 1200, delta) ||
            !number(obj, "timeoutMs", 0, 600000, timeout) ||
            !number(obj, "pollMs", 20, 5000, poll)) return {};
        if (obj.contains(QStringLiteral("doubleIntervalMs")) &&
            !number(obj, "doubleIntervalMs", 10, 1000, doubleInterval)) return {};
        return input::ElementTask{static_cast<input::ElementCommand>(command),
            text.toString().toStdU16String(), static_cast<input::Axis>(axis), delta, timeout, poll,
            doubleInterval};
    }
    if (name == QStringLiteral("click")) {
        std::int64_t count = 0;
        int interval = 0;
        if (!number(obj, "button", 0, 4, value) ||
            !number64(obj, "count", 1, count) ||
            !number(obj, "intervalMs", 0, 60000, interval)) return {};
        return input::Click{static_cast<input::Button>(value), count, interval};
    }
    if (name == QStringLiteral("mouseDown") || name == QStringLiteral("mouseUp")) {
        if (!number(obj, "button", 0, 4, value)) return {};
        const auto button = static_cast<input::Button>(value);
        if (name == QStringLiteral("mouseDown")) return input::ButtonDown{button};
        return input::ButtonUp{button};
    }
    if (name == QStringLiteral("keyTap") || name == QStringLiteral("keyDown") || name == QStringLiteral("keyUp")) {
        if (!number(obj, "vk", 8, 254, value)) return {};
        if (name == QStringLiteral("keyTap")) return input::KeyTap{value};
        if (name == QStringLiteral("keyDown")) return input::KeyDown{value};
        return input::KeyUp{value};
    }
    if (name == QStringLiteral("mouseHold")) {
        int duration = 0;
        if (!number(obj, "button", 0, 4, value) ||
            !number(obj, "durationMs", 1, 60000, duration)) return {};
        return input::MouseHold{static_cast<input::Button>(value), duration};
    }
    if (name == QStringLiteral("keyHold")) {
        int duration = 0;
        if (!number(obj, "vk", 8, 254, value) ||
            !number(obj, "durationMs", 1, 60000, duration)) return {};
        return input::KeyHold{value, duration};
    }
    if (name == QStringLiteral("movePointer")) {
        int x = 0, y = 0, duration = 0;
        if (!number(obj, "mode", 0, 2, value) ||
            !number(obj, "x", -32768, 32767, x) ||
            !number(obj, "y", -32768, 32767, y) ||
            !number(obj, "durationMs", 0, 60000, duration)) return {};
        return input::MovePointer{static_cast<input::MoveMode>(value), x, y, duration};
    }
    if (name == QStringLiteral("powershell")) {
        const auto code = obj.value(QStringLiteral("code"));
        if (!code.isString()) return {};
        return input::RunScript{code.toString().toStdU16String()};
    }
    if (name == QStringLiteral("text")) {
        const auto text = obj.value(QStringLiteral("text"));
        if (!text.isString()) return {};
        return input::TypeText{text.toString().toStdU16String()};
    }
    if (name == QStringLiteral("wheel")) {
        int delta = 0;
        if (!number(obj, "axis", 0, 1, value) ||
            !number(obj, "delta", -1200, 1200, delta) || delta == 0) return {};
        return input::Wheel{static_cast<input::Axis>(value), delta};
    }
    if (name == QStringLiteral("waitUntil")) {
        int hour = 0, minute = 0, second = 0;
        if (!number(obj, "hour", 0, 23, hour) || !number(obj, "minute", 0, 59, minute) ||
            !number(obj, "second", 0, 59, second)) return {};
        return input::WaitUntil{hour, minute, second};
    }
    if (name == QStringLiteral("wait")) {
        std::int64_t duration = 0;
        if (!number64(obj, "durationMs", 0, duration)) return {};
        return input::Wait{duration};
    }
    return {};
}
} // namespace

QJsonObject encodeInputAction(const input::Action& action) { return writeAction(action); }
QJsonObject encodeBoundInputAction(const input::Action& action, const input::StepWindow& binding) {
    auto record = writeAction(action);
    if (!binding.title.empty()) {
        record[QStringLiteral("targetTitle")] = QString::fromStdString(binding.title);
        record[QStringLiteral("targetProcess")] = QString::fromStdString(binding.process);
        record[QStringLiteral("activateWindow")] = binding.activate;
        record[QStringLiteral("waitForWindowMs")] = QString::number(binding.waitMs);
        record[QStringLiteral("enforceTargetWindow")] = binding.enforce;
    }
    if (!binding.elementName.empty() || !binding.elementAutomationId.empty()) {
        record[QStringLiteral("uiaName")] = QString::fromStdString(binding.elementName);
        record[QStringLiteral("uiaAutomationId")] = QString::fromStdString(binding.elementAutomationId);
        record[QStringLiteral("uiaClass")] = QString::fromStdString(binding.elementClass);
        record[QStringLiteral("uiaRole")] = binding.elementRole;
        record[QStringLiteral("uiaProvider")] = binding.elementProvider;
        record[QStringLiteral("uiaBrowser")] = binding.elementBrowser;
        if (!binding.elementTab.empty())
            record[QStringLiteral("uiaTab")] = QString::fromStdString(binding.elementTab);
        record[QStringLiteral("uiaOperation")] = binding.elementOperation;
        if (!binding.elementValue.empty())
            record[QStringLiteral("uiaValue")] = QString::fromStdString(binding.elementValue);
    }
    if (!binding.manualGroupId.empty()) {
        record[QStringLiteral("manualGroupId")] = QString::fromStdString(binding.manualGroupId);
        record[QStringLiteral("manualGroupTitle")] = QString::fromStdString(binding.manualGroupTitle);
    }
    return record;
}
std::optional<input::StepWindow> decodeStepWindow(const QJsonObject& record) {
    input::StepWindow binding;
    const auto groupId = record.value(QStringLiteral("manualGroupId"));
    const auto groupTitle = record.value(QStringLiteral("manualGroupTitle"));
    if (!groupId.isUndefined() || !groupTitle.isUndefined()) {
        if (!groupId.isString() || !groupTitle.isString()) return std::nullopt;
        const QString id = groupId.toString();
        const QString name = groupTitle.toString();
        // Only IDs generated by the editor are accepted. Reject malformed or
        // oversized external metadata, including embedded NULs and control chars.
        static const QRegularExpression uuid(QStringLiteral(
            "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
        if (!uuid.match(id).hasMatch() || name.trimmed().isEmpty() ||
            name.size() > 80 || name.contains(QChar(0)) ||
            std::any_of(name.begin(), name.end(), [](const QChar ch) { return ch.category() == QChar::Other_Control; }))
            return std::nullopt;
        binding.manualGroupId = id.toStdString();
        binding.manualGroupTitle = name.toStdString();
    }
    if (record.contains(QStringLiteral("targetTitle"))) {
        if (!record.value(QStringLiteral("targetTitle")).isString() ||
            !record.value(QStringLiteral("targetProcess")).isString() ||
            !record.value(QStringLiteral("activateWindow")).isBool() ||
            !number64(record, "waitForWindowMs", 0, binding.waitMs)) return std::nullopt;
        binding.title = record.value(QStringLiteral("targetTitle")).toString().toStdString();
        binding.process = record.value(QStringLiteral("targetProcess")).toString().toStdString();
        binding.activate = record.value(QStringLiteral("activateWindow")).toBool();
        const auto enforcement = record.value(QStringLiteral("enforceTargetWindow"));
        if (!enforcement.isUndefined() && !enforcement.isBool()) return std::nullopt;
        // Old scenarios required their saved target. Keep that behavior.
        binding.enforce = enforcement.isUndefined() || enforcement.toBool();
    } else if (record.contains(QStringLiteral("targetProcess")) ||
               record.contains(QStringLiteral("activateWindow")) ||
               record.contains(QStringLiteral("waitForWindowMs")) ||
               record.contains(QStringLiteral("enforceTargetWindow"))) return std::nullopt;
    const bool uiFields = record.contains(QStringLiteral("uiaName")) ||
        record.contains(QStringLiteral("uiaAutomationId")) ||
        record.contains(QStringLiteral("uiaClass")) ||
        record.contains(QStringLiteral("uiaRole")) ||
        record.contains(QStringLiteral("uiaProvider")) ||
        record.contains(QStringLiteral("uiaBrowser")) ||
        record.contains(QStringLiteral("uiaTab")) ||
        record.contains(QStringLiteral("uiaOperation")) ||
        record.contains(QStringLiteral("uiaValue"));
    if (uiFields) {
        if (!record.value(QStringLiteral("uiaName")).isString() ||
            !record.value(QStringLiteral("uiaAutomationId")).isString() ||
            !record.value(QStringLiteral("uiaClass")).isString() ||
            !record.value(QStringLiteral("uiaBrowser")).isBool() ||
            !number(record, "uiaRole", 0, 12, binding.elementRole)) return std::nullopt;
        if (record.contains(QStringLiteral("uiaProvider")) &&
            !number(record, "uiaProvider", 0, 4, binding.elementProvider)) return std::nullopt;
        const QJsonValue operation = record.value(QStringLiteral("uiaOperation"));
        if (!operation.isUndefined() && !number(record, "uiaOperation", 0, 10,
                                               binding.elementOperation)) return std::nullopt;
        const QJsonValue operationValue = record.value(QStringLiteral("uiaValue"));
        if (!operationValue.isUndefined() && !operationValue.isString()) return std::nullopt;
        if (operationValue.isString()) binding.elementValue = operationValue.toString().toStdString();
        binding.elementName = record.value(QStringLiteral("uiaName")).toString().toStdString();
        binding.elementAutomationId = record.value(QStringLiteral("uiaAutomationId")).toString().toStdString();
        binding.elementClass = record.value(QStringLiteral("uiaClass")).toString().toStdString();
        binding.elementBrowser = record.value(QStringLiteral("uiaBrowser")).toBool();
        const auto tabValue = record.value(QStringLiteral("uiaTab"));
        if (!tabValue.isUndefined() && !tabValue.isString()) return std::nullopt;
        if (tabValue.isString()) binding.elementTab = tabValue.toString().toStdString();
        if ((!binding.elementTab.empty() && !binding.elementBrowser) ||
            binding.elementTab.size() > 512 || binding.elementTab.find('\0') != std::string::npos ||
            (binding.elementName.empty() && binding.elementAutomationId.empty()) ||
            binding.title.empty() || !binding.enforce ||
            binding.elementRole < 0 || binding.elementRole > 12 ||
            (binding.elementProvider != 0 &&
              (binding.elementBrowser ||
               (binding.elementProvider == 1 && !binding.elementAutomationId.starts_with("msaa")) ||
               (binding.elementProvider == 2 && !binding.elementAutomationId.starts_with("jab")) ||
               (binding.elementProvider == 3 &&
                (!binding.elementAutomationId.starts_with("cv:") || !binding.elementClass.starts_with("v1:"))) ||
               (binding.elementProvider == 4 &&
                (!binding.elementAutomationId.starts_with("ocr:") || !binding.elementClass.starts_with("v1:"))))) ||
            binding.elementOperation < 0 || binding.elementOperation > 10 ||
            binding.elementValue.size() > 4096 ||
            binding.elementValue.find('\0') != std::string::npos ||
            binding.elementName.size() > 512 || binding.elementAutomationId.size() > 512 ||
            binding.elementClass.size() > 512 ||
            binding.elementName.find('\0') != std::string::npos ||
            binding.elementAutomationId.find('\0') != std::string::npos ||
            binding.elementClass.find('\0') != std::string::npos) return std::nullopt;
    }
    if (binding.title.size() > 1024 || binding.process.size() > 260 ||
        (binding.title.empty() && (!binding.process.empty() || binding.activate || binding.waitMs)) ||
        (!binding.enforce && (binding.activate || binding.waitMs)))
        return std::nullopt;
    return binding;
}
std::optional<input::Action> decodeInputAction(const QJsonObject& data) { return readAction(data); }

QVariantMap encodeInputPlan(const input::Plan& plan) {
    QJsonObject obj;
    obj.insert(QStringLiteral("planVersion"), 1);
    obj.insert(QStringLiteral("repetitions"), plan.repetitions);
    obj.insert(QStringLiteral("passDelayMs"), QString::number(plan.passDelayMs));
    obj.insert(QStringLiteral("requireTargetWindow"), plan.requireTargetWindow);
    QJsonArray list;
    for (std::size_t i = 0; i < plan.actions.size(); ++i) {
        list.append(encodeBoundInputAction(plan.actions[i],
            i < plan.stepWindows.size() ? plan.stepWindows[i] : input::StepWindow{}));
    }
    obj.insert(QStringLiteral("actions"), list);
    return obj.toVariantMap();
}

std::optional<input::Plan> decodeInputPlan(const QVariantMap& values, QString* error) {
    const auto obj = QJsonObject::fromVariantMap(values);
    input::Plan candidate;
    int version = 0;
    if (!number(obj, "planVersion", 1, 1, version) ||
        !number(obj, "repetitions", 1, 2147483647, candidate.repetitions) ||
        !number64(obj, "passDelayMs", 0, candidate.passDelayMs)) {
        fail(error, QStringLiteral("Invalid plan header or unsupported version"));
        return {};
    }
    // Older scenarios have no window-binding metadata and remain compatible.
    const auto binding = obj.value(QStringLiteral("requireTargetWindow"));
    if (!binding.isUndefined() && !binding.isBool()) {
        fail(error, QStringLiteral("Invalid window binding flag"));
        return {};
    }
    candidate.requireTargetWindow = binding.isBool() && binding.toBool();
    const auto list = obj.value(QStringLiteral("actions"));
    if (!list.isArray() || list.toArray().isEmpty() || list.toArray().size() > 512) {
        fail(error, QStringLiteral("Invalid number of actions"));
        return {};
    }
    for (const auto& record : list.toArray()) {
        if (!record.isObject()) {
            fail(error, QStringLiteral("Invalid action record"));
            return {};
        }
        auto action = readAction(record.toObject());
        if (!action) {
            fail(error, QStringLiteral("Unsupported action or invalid parameters"));
            return {};
        }
        candidate.actions.push_back(std::move(*action));
        const auto stepBinding = decodeStepWindow(record.toObject());
        if (!stepBinding) {
            fail(error, QStringLiteral("Invalid action window binding"));
            return {};
        }
        candidate.stepWindows.push_back(*stepBinding);
    }
    const auto validation = input::validate(candidate);
    if (!validation.empty()) {
        fail(error, QString::fromStdString(validation));
        return {};
    }
    return candidate;
}
} // namespace clicknova::storage
