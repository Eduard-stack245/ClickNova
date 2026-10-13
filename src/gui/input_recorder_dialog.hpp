// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_recording.hpp"
#include <QDialog>
#include <QPointer>
#include <QDateTime>
#include <QString>
#include <functional>
#include <set>
#include <vector>
#include <utility>

class QCheckBox;
class QLabel;
class QPushButton;
class QCloseEvent;
class QHideEvent;
class QTimer;

// Always-visible, explicitly controlled recorder. Recovery checkpoints are saved
// locally only after Record; hooks exist only during Start/Resume.
class InputRecorderDialog final : public QDialog {
public:
    using Finished = std::function<void(std::vector<clicknova::recording::Sample>, bool cancelled, std::uint64_t durationMs)>;
    explicit InputRecorderDialog(bool russian, Finished finished);
    ~InputRecorderDialog() override;
    void discard() { stop(true); }
    // A global Stop shortcut must preserve captured actions for review.
    void finishForReview() { stop(false); }
    void bringToFront();
    bool recordingStarted() const noexcept { return started_; }
    QString checkpointFile() const { return checkpointFile_; }
private:
    class NativeHooks;
    void start();
    void pauseOrResume();
    void stop(bool cancelled);
    void record(const clicknova::recording::Sample& event);
    void updateUi();
    void updateProgress();
    void maybeCheckpoint(bool force = false);
    void closeRecordedHolds();
    void closeEvent(QCloseEvent* event) override;
    void reject() override;
    void hideEvent(QHideEvent* event) override;
    bool russian_{};
    bool started_{};
    bool paused_{};
    bool completed_{};
    bool keyboardEnabled_{true};
    bool textEnabled_{true};
    bool movementEnabled_{true};
    std::set<clicknova::input::Button> heldButtons_;
    std::set<int> heldKeys_;
    std::uint64_t startedTick_{};
    std::uint64_t pausedTick_{};
    std::uint64_t totalPaused_{};
    std::uint64_t lastMoveTick_{};
    int lastMoveX_{};
    int lastMoveY_{};
    Finished finished_;
    std::vector<clicknova::recording::Sample> samples_;
    QString checkpointFile_;
    QDateTime checkpointStarted_;
    std::uint64_t lastCheckpointTick_{};
    bool checkpointPending_{false};
    bool checkpointErrorShown_{false};
    int previousMeaning_{-1};
    clicknova::recording::WindowInfo previousWindow_;

    NativeHooks* hooks_{};
    QLabel* info_{};
    QLabel* count_{};
    QCheckBox* keyboard_{};
    QCheckBox* text_{};
    QLabel* window_{};
    QCheckBox* movement_{};
    QTimer* progressTimer_{};
    QPushButton* start_{};
    QPushButton* pause_{};
    QPushButton* stop_{};
    QPushButton* cancel_{};
};
