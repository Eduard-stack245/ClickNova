// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <mutex>
namespace clicknova::platform::x11 {
std::mutex& errorHandlerMutex();
void ensureThreadSafety();
}
