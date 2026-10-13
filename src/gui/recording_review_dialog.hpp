// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_actions.hpp"
#include "clicknova/input_recording.hpp"
#include <QDialog>
#include <cstddef>
#include <cstdint>
#include <vector>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

// Explicit review before imported input reaches the editable scenario.
// Imported actions enter the editor only after explicit review. The editor
// autosaves its local draft; named scenario saving remains a separate action.
class RecordingReviewDialog final : public QDialog {
public:
    enum class Decision { Cancel, Replace, Append };
    RecordingReviewDialog(std::vector<clicknova::input::Action> actions,
                          std::size_t capturedEvents, std::uint64_t recordedMs,
                          bool russian, QWidget* parent = nullptr,
                          std::vector<clicknova::recording::WindowInfo> windows = {});
    const std::vector<clicknova::recording::WindowInfo>& windows() const noexcept { return windows_; }
    Decision decision() const noexcept { return decision_; }
    bool saveRequested() const;
    const std::vector<clicknova::input::Action>& actions() const noexcept { return actions_; }
private:
    QString text(const char* english, const char* russian) const;
    void refresh(int selection);
    void editSelected();
    void removeSelected();
    void moveSelected(int distance);
    void import(Decision decision);
    QString actionName(const clicknova::input::Action& action) const;
    QString actionDetails(const clicknova::input::Action& action) const;

    std::vector<clicknova::input::Action> actions_;
    std::vector<clicknova::recording::WindowInfo> windows_;
    std::size_t capturedEvents_{};
    std::uint64_t recordedMs_{};
    bool russian_{};
    Decision decision_{Decision::Cancel};
    QTableWidget* table_{};
    QLabel* stats_{};
    QLabel* feedback_{};
    QCheckBox* save_{};
    QPushButton* edit_{};
    QPushButton* remove_{};
    QPushButton* up_{};
    QPushButton* down_{};
    QPushButton* replace_{};
    QPushButton* append_{};
};
