// SPDX-License-Identifier: Apache-2.0
#include "input_plan_codec.hpp"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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
    return record;
}
std::optional<input::StepWindow> decodeStepWindow(const QJsonObject& record) {
    input::StepWindow binding;
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
