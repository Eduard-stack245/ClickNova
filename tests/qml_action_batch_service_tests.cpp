// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_batch_service.hpp"
#include "../src/gui/qml/action_workspace.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace clicknova::input;
using namespace clicknova::gui::qml;
namespace batch = clicknova::gui::qml::action_batch;

int main() {
    unsigned checks = 0;
    const auto require = [&](bool truth, const char* message) {
        ++checks;
        if (!truth) { std::cerr << "batch service: " << message << "\n"; std::exit(1); }
    };
    const auto id = [](const Action& action) -> int {
        return static_cast<int>(std::get<Wait>(action).durationMs);
    };
    const auto fixture = [](int n) {
        std::vector<Action> actions;
        std::vector<StepWindow> windows;
        for (int i = 0; i < n; ++i) {
            actions.emplace_back(Wait{i});
            StepWindow binding;
            binding.title = "window_" + std::to_string(i);
            binding.manualGroupId = "group_" + std::to_string(i / 4);
            binding.elementName = "selector_" + std::to_string(i);
            windows.push_back(std::move(binding));
        }
        return std::pair{std::move(actions), std::move(windows)};
    };
    const auto aligned = [&](const auto& actions, const auto& windows) {
        require(actions.size() == windows.size(), "actions and windows size aligned");
        for (std::size_t i = 0; i < actions.size(); ++i) {
            const auto n = id(actions[i]);
            require(windows[i].title == "window_" + std::to_string(n), "binding follows action");
            require(windows[i].elementName == "selector_" + std::to_string(n), "UI selector follows action");
            require(windows[i].manualGroupId == "group_" + std::to_string(n / 4), "group follows action");
        }
    };
    {
        auto [actions, windows] = fixture(5);
        require(batch::propose(actions, windows, {}, batch::Operation::Erase).error == batch::Error::EmptySelection,
                "empty selection rejected");
        require(batch::propose(actions, windows, {-1}, batch::Operation::Erase).error == batch::Error::InvalidSelection,
                "negative row rejected");
        require(batch::propose(actions, windows, {5}, batch::Operation::Erase).error == batch::Error::InvalidSelection,
                "out-of-range row rejected");
        auto missing = windows; missing.pop_back();
        require(batch::propose(actions, missing, {2}, batch::Operation::Erase).error == batch::Error::UnalignedBindings,
                "unaligned bindings rejected");
        require(batch::propose(actions, windows, {2, 2}, batch::Operation::Erase).status == batch::Status::Updated,
                "duplicate indices deduplicated");
        auto result = batch::propose(actions, windows, {3, 1}, batch::Operation::Duplicate);
        require(result.status == batch::Status::Updated, "duplicate success");
        require(result.selection == std::vector<int>({2, 5}), "duplicated rows selected");
        require(result.actions.size() == 7, "two added");
        require(id(result.actions[1]) == 1 && id(result.actions[2]) == 1 && id(result.actions[4]) == 3 && id(result.actions[5]) == 3,
                "selected items duplicated immediately after originals");
        aligned(result.actions, result.windows);
        result = batch::propose(actions, windows, {1, 3}, batch::Operation::Erase);
        require(result.status == batch::Status::Updated, "delete success");
        require(result.selection == std::vector<int>({1}), "delete selects next row");
        require(result.actions.size() == 3 && id(result.actions[1]) == 2, "deleted correct indices");
        aligned(result.actions, result.windows);
        result = batch::propose(actions, windows, {0, 1}, batch::Operation::MoveUp);
        require(result.status == batch::Status::Unchanged, "top block cannot move up");
        result = batch::propose(actions, windows, {2, 3}, batch::Operation::MoveUp);
        require(result.status == batch::Status::Updated && result.selection == std::vector<int>({1, 2}),
                "contiguous block moves up together");
        require(id(result.actions[1]) == 2 && id(result.actions[2]) == 3, "block order preserved");
        aligned(result.actions, result.windows);
        result = batch::propose(actions, windows, {1, 3}, batch::Operation::MoveDown);
        require(result.status == batch::Status::Updated && result.selection == std::vector<int>({2, 4}),
                "discontiguous rows move down independently");
        aligned(result.actions, result.windows);
        require(batch::propose(actions, windows, {1, 2, 3, 4}, batch::Operation::MoveDown).status ==
                batch::Status::Unchanged, "bottom block no-op");
        result = batch::propose(actions, windows, {0, 1, 2, 3, 4}, batch::Operation::Erase);
        require(result.status == batch::Status::Updated && result.actions.empty() && result.selection.empty(),
                "delete-all produces valid empty plan");
    }
    {
        auto [actions, windows] = fixture(511);
        require(batch::propose(actions, windows, {0, 1}, batch::Operation::Duplicate).error == batch::Error::LimitExceeded,
                "limit 512 cannot be exceeded");
        auto one = batch::propose(actions, windows, {0}, batch::Operation::Duplicate);
        require(one.status == batch::Status::Updated && one.actions.size() == 512, "boundary allows 512 actions");
    }
    {
        auto [actions, windows] = fixture(5);
        ActionListHistory history;
        ActionWorkspace workspace(actions, windows, history);
        require(workspace.select(2), "selection for history");
        const auto valid = [](const std::vector<Action>&) { return true; };
        auto result = workspace.applyBatch({1, 3}, batch::Operation::Duplicate, valid);
        require(result.status == batch::Status::Updated && history.canUndo(), "batch creates one history checkpoint");
        require(actions.size() == 7 && workspace.selectedRow() == 2, "selection follows generated copy");
        require(workspace.undo() && actions.size() == 5, "undo restores exact size");
        aligned(actions, windows);
        require(workspace.redo() && actions.size() == 7, "redo restores duplicate");
        aligned(actions, windows);
        history.clear();
        const auto oldSize = actions.size();
        result = workspace.applyBatch({0}, batch::Operation::MoveUp, valid);
        require(result.status == batch::Status::Unchanged && !history.canUndo(), "no-op does not enter history");
        result = workspace.applyBatch({0}, batch::Operation::Erase, {});
        require(result.status == batch::Status::Rejected && !history.canUndo() && actions.size() == oldSize,
                "missing validation callback fails closed");
        result = workspace.applyBatch({0}, batch::Operation::Erase, [](const auto&) { return false; });
        require(result.status == batch::Status::Updated, "invalid input plan may be repaired by deletion");
        require(workspace.undo() && actions.size() == oldSize, "repair remains undoable");
    }
    {
        // A valid held-input pair cannot be broken by a user moving one half.
        std::vector<Action> actions{ButtonDown{Button::Left}, Wait{25}, ButtonUp{Button::Left}};
        std::vector<StepWindow> windows(3);
        ActionListHistory history;
        ActionWorkspace workspace(actions, windows, history);
        const auto valid = [](const std::vector<Action>& a) {
            bool down = false;
            for (const auto& step : a) {
                if (std::holds_alternative<ButtonDown>(step)) { if (down) return false; down = true; }
                if (std::holds_alternative<ButtonUp>(step)) { if (!down) return false; down = false; }
            }
            return !down;
        };
        auto result = workspace.applyBatch({2}, batch::Operation::MoveUp, valid);
        require(result.status == batch::Status::Updated, "legal movement allowed");
        require(workspace.undo(), "undo legal movement");
        result = workspace.applyBatch({2}, batch::Operation::Duplicate, valid);
        require(result.status == batch::Status::Rejected && !history.canUndo(), "duplicating release breaks pair and is rejected");
        result = workspace.applyBatch({0}, batch::Operation::Erase, valid);
        require(result.status == batch::Status::Rejected && !history.canUndo(), "deleting press breaks pair and is rejected");
        require(actions.size() == 3 && std::holds_alternative<ButtonDown>(actions[0]), "rejected changes untouched");
    }
    // Deterministic property testing of selection and exact binding correspondence.
    std::mt19937 engine(0xC11C0A);
    for (int trial = 0; trial < 2000; ++trial) {
        const int count = 1 + static_cast<int>(engine() % 60);
        auto [actions, windows] = fixture(count);
        std::vector<int> rows;
        for (int i = 0; i < count; ++i) if ((engine() % 5) == 0) rows.push_back(i);
        if (rows.empty()) rows.push_back(static_cast<int>(engine() % count));
        const auto kind = static_cast<batch::Operation>(engine() % 4);
        auto result = batch::propose(actions, windows, rows, kind);
        require(result.status != batch::Status::Rejected, "generated valid selection never rejected");
        aligned(result.actions, result.windows);
        require(std::is_sorted(result.selection.begin(), result.selection.end()), "selection remains sorted");
        for (int row : result.selection) require(row >= 0 && row < static_cast<int>(result.actions.size()),
                                                "selection remains valid after mutation");
    }
    std::cout << "ActionBatchService: " << checks << " assertions passed\n";
}
