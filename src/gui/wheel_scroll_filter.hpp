// SPDX-License-Identifier: Apache-2.0
#pragma once

class QApplication;

// Keep the mouse wheel for page scrolling instead of changing form values.
void installWheelScrollFilter(QApplication& app);
