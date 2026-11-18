// SPDX-License-Identifier: MIT
#pragma once
#include <mutex>
namespace clicknova::platform::x11 {
std::mutex& errorHandlerMutex();
void ensureThreadSafety();
}
