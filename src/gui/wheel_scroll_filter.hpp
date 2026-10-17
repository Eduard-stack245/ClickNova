// SPDX-License-Identifier: Apache-2.0
#pragma once
class QApplication;
class QScrollArea;
class QTableWidget;
class QStackedWidget;

void installWheelScrollFilter(QApplication& app);
// Give every workspace reliable page scrolling, even over child controls.
void configurePageWheelRouting(QStackedWidget* pages);
// Route the Steps page between its list and outer scroll by last click, not hover.
void configureStepWheelRouting(QTableWidget* table, QScrollArea* page);
void resetStepWheelRouting();
