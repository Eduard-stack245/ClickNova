// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <string_view>
#include <QByteArray>
namespace clicknova::accessibility::candidates { struct Candidate; }

#include "clicknova/input_actions.hpp"
#include "clicknova/input_recording.hpp"
#include "clicknova/action_list_edit.hpp"
#include "qml/action_workspace.hpp"
#include "clicknova/window_target.hpp"
#ifdef CLICKNOVA_WITH_X11
#include "clicknova/x11_input_backend.hpp"
#endif
#include <optional>
#include <memory>
#include <chrono>
#include "../storage/scenario_store.hpp"
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QPoint>
#include <QPointer>
#include <stop_token>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
#include <QSet>
#include <QHash>
#include <QImage>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QPlainTextEdit;
class QTimeEdit;
class QMenu;
class QEvent;
class QTableWidget;
class QVBoxLayout;
class QDialog;
class ActionTableWidget;
class InputRecorderDialog;

// Shared sequence workspace for clicks, keyboard, text and recorded actions.
class InputLabWidget final : public QWidget {
public:
    explicit InputLabWidget(QWidget* parent = nullptr);
    ~InputLabWidget() override;
    void setRussian(bool enabled);
    bool isRunning() const noexcept;
    bool isPlaybackActive() const noexcept;
    void cancel();
    void requestRun() { beginPlayback(); }
    QPushButton* runControl() const { return run_; }
    QPushButton* stopControl() const { return stop_; }
    QPushButton* recoveryControl() const { return recoverRecording_; }
    QComboBox* savedPlanPicker() const { return savedPlans_; }
    QPushButton* savePlanControl() const { return savePlan_; }
    QPushButton* loadPlanControl() const { return loadPlan_; }
    QPushButton* deletePlanControl() const { return deletePlan_; }
    void hideEmbeddedSavedLibrary();
    void setStatusSink(std::function<void(const QString&)> sink) { statusSink_ = std::move(sink); }
    void setCanBegin(std::function<bool()> guard) { canBegin_ = std::move(guard); }
    void replaceActions(const clicknova::input::Plan& plan);
    // Reuse the configured global stop shortcut. Playback cannot start if the
    // shortcut could not be reserved, so minimizing never hides the only stop.
    void setStartShortcut(std::function<QString()> label);
    void setLegacyImporter(std::function<void()> importer);
    void refreshLibrary();
    // Read-only owner-scoped access, not a second window registry.
    InputRecorderDialog* activeRecorder() const noexcept { return recorder_; }
    // Typed QML window binding. No native HWND is persisted in action files.
    bool bindTargetWindowFromQml(const clicknova::platform::WindowTarget& target,
                                 bool allowCoordinateRetarget);
    bool bindStepWindowFromQml(int expectedRow, const clicknova::platform::WindowTarget& target,
                               bool allowElementReset);
    bool actionPlanExistsForQml(const QString& name);
    bool saveActionPlanFromQml(const QString& name, bool replaceExisting);
    bool loadActionPlanFromQml(const QString& name);
    bool deleteActionPlanFromQml(const QString& name);
    bool reviewSavedRecordingFromQml(const QString& fileName);
    // Verified local recovery import controlled by the QML confirmation dialog.
    // This method never opens QWidget prompts or executes recorded actions.
    bool importSavedRecordingFromQml(const QString& fileName, bool append);
    // QML-only review handoff; the native dialog remains a safe fallback if
    // a finished recording cannot be durably checkpointed.
    void setQmlRecordingReviewEnabled(bool enabled) noexcept { qmlRecordingReviewEnabled_ = enabled; }
    QString pendingRecordingReviewForQml() const;
    bool dismissPendingRecordingReviewForQml();
    bool deleteSavedRecordingFromQml(const QString& fileName);
    bool validateEditorFromQml(int row) const;
    bool editActionFromQml(int row, const QString& field, const QString& value);
    std::string actionRevisionForPresentation() const { return actionWorkspace_.revision(); }
    bool editActionFieldsFromQml(int row,
        const std::vector<clicknova::gui::qml::ActionWorkspace::FieldEdit>& edits,
        std::string_view expectedRevision);
    bool setBrowserTabScopeFromQml(int row, bool enabled, const QByteArray& titleUtf8);
    bool bindVerifiedUiaElementFromQml(int row, const std::string& title,
        const std::string& process, const clicknova::accessibility::candidates::Candidate& selected);
    bool undoActionFromQml();
    bool redoActionFromQml();
    std::optional<std::vector<int>> batchActionsFromQml(
        const QString& operation, const std::vector<int>& rows);
    bool canUndoActionFromQml() const { return !isRunning() && actionWorkspace_.canUndo(); }
    bool canRedoActionFromQml() const { return !isRunning() && actionWorkspace_.canRedo(); }
    const std::vector<clicknova::input::Action>& actionsForPresentation() const noexcept { return actions_; }
    const std::vector<clicknova::input::StepWindow>& windowBindingsForPresentation() const noexcept { return stepWindows_; }
    int selectedActionRowForPresentation() const;
    bool selectActionRowForPresentation(int row);
    // Typed public actions for the Qt Quick inspector; native validation stays authoritative.
    bool addActionFromQml(int actionType);
    bool canEditActionFromQml(int row) const;
    void applyLineFieldFromQml(const QString& name);
    void refreshStartLabel() { updateStartLabel(); }
    void setStopHotkeyController(std::function<bool(bool)> controller,
                                 std::function<QString()> label);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum Type { MouseClick, KeyTap, KeyDown, KeyUp, MouseDown, MouseUp, Scroll, Wait, MouseHoldTimed, KeyHoldTimed, TypeText, PointerMove, PowerShell, WaitForTime };
    void addAction();
    void addActionOfType(int type);
    void insertActionAt(int row, int type);
    clicknova::input::Action defaultActionOfType(int type) const;
    void clearAllActions();
    void undoActionEdit();
    void redoActionEdit();
    void checkpointActions();
    void showActionMenu(int row, const QPoint& globalPos);
    void renameStep(int row);
    void chooseStepImage(int row);
    void viewStepImage(int row);
    void moveActionRow(int from, int to);
    void duplicateRow(int row);
    void deleteRow(int row);
    std::vector<int> selectedActionRows() const;
    void refreshSelectedRows(const std::vector<int>& rows);
    void deleteSelectedRows();
    void duplicateSelectedRows();
    void moveSelectedRows(int direction);
    void moveSelectedRowsTo(const std::vector<int>& rows, int insertion);
    void copySelectedRows();
    void scheduleAutosave();
    bool writeAutosave();
    void restoreAutosave();
    void copyRow(int row);
    void pasteActionAt(int row);
    bool canPasteAction() const;
    void updateSelected();
    void openStepEditor(int row);
    void finishStepEditor(bool save);
    void loadElementTaskEditor(const clicknova::input::Action& action);
    void updateElementTaskEditor();
    void chooseStepWindow();
    void searchStepUiElements();
    void clearStepUiElement();
    void clearStepWindow();
    void makeWindowBindingsInformational(bool selectedOnly);
    void editStepWindow();
    void refreshStepWindowEditor(int row);
    void normalizeStepWindows();
    bool numericInputsValid() const;
    std::int64_t waitMilliseconds() const;
    std::int64_t passMilliseconds() const;
    void selectAction(int index);
    void refreshTable(int selectRow);
    void refreshTimingColumns();
    void openScenarioCheck();
    void applyStepVisibility();
    void navigateStepMatches(bool forward);
    void toggleStepGroup(int row);
    void createManualGroup();
    void renameManualGroup();
    void ungroupManualSteps();
    bool selectedManualGroup(int& first, int& end) const;
    void resizeStepList(int pixels);
    void detachStepList();
    int stepCategory(int row) const;
    void updateFields();
    clicknova::input::Action currentEditorAction() const;
    void loadEditor(const clicknova::input::Action& action);
    QString actionName(const clicknova::input::Action& action) const;
    QString actionDetails(const clicknova::input::Action& action) const;
    void updateButtons();
    void reportStatus(const QString& message);
    void reportActionChange(const QString& english, const QString& russian, int count);
    void refreshRecoveryAvailability();
    void refreshSavedPlans(const QString& selected = {});
    void savePlan();
    void loadPlan();
    void deletePlan();
    void loadLegacyPlan(const QString& name, clicknova::storage::Kind kind);
    void updateStartLabel();
    void beginRecording();
    void recoverRecording();
    void reviewCapturedRecording(std::vector<clicknova::recording::Sample> samples,
                                 bool cancelled, std::uint64_t durationMs,
                                 const QString& checkpointFile);
    void beginPlayback(std::optional<clicknova::input::PlaybackCursor> cursor = std::nullopt);
    void continueFailedPlayback();
    void startPlayback();
    void stopPlayback();
    void onPlaybackProgress(const clicknova::input::PlaybackProgress& progress);
    void openExecutionReport();
    QString executionReportText() const;
    void refreshNextStepTime();
    void refreshWaitPreview();
    void clearNextStepTime();
    void finishPlayback(const clicknova::input::Result& result);
    QString trText(const char* en, const char* ru) const;

    std::vector<clicknova::input::Action> actions_;
    std::vector<clicknova::input::StepWindow> stepWindows_;
    clicknova::input::ActionListHistory actionHistory_;
    clicknova::gui::qml::ActionWorkspace actionWorkspace_{actions_, stepWindows_, actionHistory_};
    // Transient binding only: never persisted as an HWND or PID.
    std::optional<clicknova::platform::WindowTarget> targetWindow_;
    bool requireWindowBinding_{false};
    bool russian_{false};
    bool updating_{false};
    bool countdown_{false};
    bool running_{false};
    bool stopHotkeyActive_{false};
    bool recording_{false};
    bool qmlRecordingReviewEnabled_{false};
    // Local checkpoint basename only; never an HWND or captured text.
    QString pendingRecordingReviewName_;
    bool nextStepPending_{false};
    bool waitingForFirst_{false};
    std::int64_t nextStepDelayMs_{0};
    int upcomingStep_{1};
    int upcomingPass_{1};
    std::chrono::steady_clock::time_point nextStepStarted_{};
    std::uint64_t playbackGeneration_{0};
    struct ExecutedStep {
        std::size_t stepIndex{0};
        int passNumber{1};
        std::int64_t elapsedMs{0};
        bool succeeded{false};
    };
    std::vector<ExecutedStep> executionRows_;
    QStringList executionStepKinds_; // generic action type only; never action details
    bool executionRowsTruncated_{false};
    QString executionOutcome_;
    QDateTime executionStartedAt_;
    QDateTime executionFinishedAt_;
    std::chrono::steady_clock::time_point executionStartedSteady_{};
    std::int64_t executionElapsedMs_{0};
    int executionTotalSteps_{0};
    int executionTotalPasses_{0};
    // Volatile continuation: never saved to disk, cleared on stop/completion.
    std::optional<clicknova::input::PlaybackCursor> resumeCursor_;
    std::optional<clicknova::input::PlaybackCursor> pendingStartCursor_;
    std::optional<clicknova::input::Plan> lastStartedPlan_;
    std::optional<clicknova::platform::WindowTarget> lastStartedTarget_;
    std::string resumeRevision_;
    InputRecorderDialog* recorder_{nullptr};
    std::function<bool(bool)> stopHotkeyController_;
    std::function<QString()> stopHotkeyLabel_;
    std::function<QString()> startHotkeyLabel_;
    std::function<void()> legacyImporter_;
    std::function<bool()> canBegin_;
    std::function<void(const QString&)> statusSink_;
    bool hasRecovery_{false};
    clicknova::storage::ScenarioStore scenarioStore_;
    std::jthread worker_;
#ifdef CLICKNOVA_WITH_X11
    std::unique_ptr<clicknova::input::X11EmergencyStop> x11Stop_;
#endif
    ActionTableWidget* table_{nullptr};
    QWidget* stepsPanel_{nullptr};
    QVBoxLayout* stepsRootLayout_{nullptr};
    QLineEdit* stepSearch_{nullptr};
    QComboBox* stepFilter_{nullptr};
    QPushButton* previousStepMatch_{nullptr};
    QPushButton* nextStepMatch_{nullptr};
    QLabel* stepCount_{nullptr};
    QWidget* stepResizeGrip_{nullptr};
    QPushButton* checkScenario_{nullptr};
    QPushButton* resumeFailed_{nullptr};
    QPushButton* expandGroups_{nullptr};
    QPushButton* collapseGroups_{nullptr};
    QPushButton* enlargeSteps_{nullptr};
    QPushButton* shrinkSteps_{nullptr};
    QPushButton* detachSteps_{nullptr};
    QPushButton* createGroup_{nullptr};
    QPushButton* renameGroup_{nullptr};
    QPushButton* ungroupSteps_{nullptr};
    QPushButton* duplicateStep_{nullptr};
    QPushButton* deleteStep_{nullptr};
    QPushButton* moveStepUp_{nullptr};
    QPushButton* moveStepDown_{nullptr};
    QPushButton* undoStep_{nullptr};
    QPushButton* redoStep_{nullptr};
    QPointer<QDialog> detachedSteps_;
    QSet<int> collapsedStepGroups_;
    QSet<QString> collapsedManualGroups_;
    std::vector<int> manualEndByStart_;
    std::vector<int> manualStartByRow_;
    std::vector<int> groupEndByStart_;
    std::vector<int> groupStartByRow_;
    bool resizingSteps_{false};
    int gripStartY_{0};
    int gripStartHeight_{0};
    int attachedHeight_{330};
    // Only the currently open menu has the discoverable object name. Closing
    // a menu does not always delete it before the next GUI event is processed.
    QPointer<QMenu> activeActionMenu_;
    QPushButton* chooseWindow_{nullptr};
    QPushButton* clearWindow_{nullptr};
    QPushButton* chooseStepWindow_{nullptr};
    QPushButton* searchStepUi_{nullptr};
    QPushButton* clearStepUi_{nullptr};
    QPushButton* clearStepWindow_{nullptr};
    QLabel* stepWindowStatus_{nullptr};
    QCheckBox* stepEnforce_{nullptr};
    QCheckBox* stepActivate_{nullptr};
    QLabel* stepWindowTimeoutLabel_{nullptr};
    QLineEdit* stepWindowWait_{nullptr};
    QLabel* windowStatus_{nullptr};
    QComboBox* type_{nullptr};
    QComboBox* savedPlans_{nullptr};
    QComboBox* mouseButton_{nullptr};
    QComboBox* keyPreset_{nullptr};
    QComboBox* wheelAxis_{nullptr};
    QComboBox* moveMode_{nullptr};
    QSpinBox* virtualKey_{nullptr};
    QLineEdit* clickCount_{nullptr};
    QSpinBox* clickInterval_{nullptr};
    QSpinBox* wheelDelta_{nullptr};
    QLineEdit* waitExpression_{nullptr};
    QLabel* waitSyntaxHint_{nullptr};
    QTimeEdit* waitClock_{nullptr};
    QLabel* waitClockLabel_{nullptr};
    QLabel* waitClockPreview_{nullptr};
    QSpinBox* holdDuration_{nullptr};
    QSpinBox* moveX_{nullptr};
    QSpinBox* moveY_{nullptr};
    QSpinBox* moveDuration_{nullptr};
    QPlainTextEdit* textInput_{nullptr};
    QPlainTextEdit* scriptInput_{nullptr};
    QLabel* scriptInputLabel_{nullptr};
    QLabel* autosaveStatus_{nullptr};
    class QTimer* autosaveTimer_{nullptr};
    bool restoringDraft_{false};
    bool autosaveEnabled_{true};
    QLabel* holdDurationLabel_{nullptr};
    QLabel* moveModeLabel_{nullptr};
    QLabel* moveXLabel_{nullptr};
    QLabel* moveYLabel_{nullptr};
    QLabel* moveDurationLabel_{nullptr};
    QLabel* textInputLabel_{nullptr};
    QPushButton* captureMouse_{nullptr};
    QPushButton* captureKey_{nullptr};
    QSpinBox* repetitions_{nullptr};
    QLineEdit* passDelay_{nullptr};
    QCheckBox* keepAwake_{nullptr};
    QLabel* typeLabel_{nullptr};
    QLabel* mouseLabel_{nullptr};
    QLabel* keyLabel_{nullptr};
    QLabel* keyPresetLabel_{nullptr};
    QLabel* clickCountLabel_{nullptr};
    QLabel* clickIntervalLabel_{nullptr};
    QLabel* wheelAxisLabel_{nullptr};
    QLabel* wheelDeltaLabel_{nullptr};
    QLabel* waitLabel_{nullptr};
    QLabel* waitNextAt_{nullptr};
    QLabel* nextStepTime_{nullptr};
    class QTimer* nextStepTimer_{nullptr};
    QLabel* repetitionsLabel_{nullptr};
    QLabel* passDelayLabel_{nullptr};
    QLabel* help_{nullptr};
    QLabel* quickHeading_{nullptr};
    QLabel* stepsHeading_{nullptr};
    QLabel* status_{nullptr};
    QPushButton* reportExecution_{nullptr};
    QPushButton* copyDiagnostic_{nullptr};
    QString lastDiagnostic_;
    QGroupBox* editorGroup_{nullptr};
    QPointer<QDialog> stepEditorDialog_;
    QPushButton* closeStepEditor_{nullptr};
    QPushButton* cancelStepEditor_{nullptr};
    int editingStepRow_{-1};
    std::vector<clicknova::input::Action> editorOriginalActions_;
    std::vector<clicknova::input::StepWindow> editorOriginalWindows_;
    QGroupBox* elementTaskGroup_{nullptr};
    QLabel* elementTaskSummary_{nullptr};
    QLabel* elementTaskTextLabel_{nullptr};
    QLineEdit* elementTaskText_{nullptr};
    QLabel* elementTaskAxisLabel_{nullptr};
    QComboBox* elementTaskAxis_{nullptr};
    QLabel* elementTaskAmountLabel_{nullptr};
    QSpinBox* elementTaskAmount_{nullptr};
    QLabel* elementTaskTimeoutLabel_{nullptr};
    QSpinBox* elementTaskTimeout_{nullptr};
    QLabel* elementTaskPollLabel_{nullptr};
    QSpinBox* elementTaskPoll_{nullptr};
    QLabel* elementTaskDoubleLabel_{nullptr};
    QSpinBox* elementTaskDouble_{nullptr};
    QGroupBox* savedGroup_{nullptr};
    QPushButton* quickClick_{nullptr};
    QPushButton* quickText_{nullptr};
    QPushButton* quickKey_{nullptr};
    QPushButton* quickWait_{nullptr};
    QPushButton* quickWaitUntil_{nullptr};
    QPushButton* quickCode_{nullptr};
    QPushButton* selectAll_{nullptr};
    QPushButton* savedToggle_{nullptr};
    QPushButton* importCurrentLegacy_{nullptr};
    QPushButton* record_{nullptr};
    QPushButton* recoverRecording_{nullptr};
    QPushButton* run_{nullptr};
    QPushButton* stop_{nullptr};
    QPushButton* savePlan_{nullptr};
    QPushButton* loadPlan_{nullptr};
    QPushButton* deletePlan_{nullptr};
};
