// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/uia_worker_protocol.hpp"
#include <QString>
#include <QStringList>
#include <stop_token>

namespace clicknova::gui::qml::uia_transport {
enum class Outcome { Success, Cancelled, TimedOut, Unavailable, Malformed };
struct Transaction { Outcome outcome{Outcome::Unavailable}; accessibility::ipc::Reply reply{}; };
// An explicit executable and argv are accepted ONLY by this low-level module,
// so tests can use a harmless fixture. Production always selects its own binary.
// Never use a shell or pass selectors in command-line arguments.
Transaction transact(const QString& executable, const QStringList& arguments,
                     const accessibility::ipc::Request& request, int timeoutMs,
                     std::stop_token cancel);
} // namespace clicknova::gui::qml::uia_transport
