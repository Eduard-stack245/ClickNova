# SPDX-License-Identifier: MIT
"""Classic Qt Widgets build must not regress to a QML UI."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/"src/gui/main.cpp").read_text(encoding="utf-8")
cmake=(root/"CMakeLists.txt").read_text(encoding="utf-8")
assert src.count("window->show();")==1
assert "createClickNovaMainWindow()" in src
assert "runClickNovaQml" not in src
assert "QQmlApplicationEngine" not in src
assert "CLIC"+"KNOVA_HAS_QML" not in src
assert "runUiaWorker()" in src and "runVisualWorker()" in src
assert 'if (app.arguments().contains(QStringLiteral("--qml-smoke"))) return 2;' in src
assert "find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Network)" in cmake
for requirement in ("Qt6::Qml", "Qt6::Quick", "Qt6::QuickControls2",
                    "src/gui/qml/ui.qrc", "src/gui/qml/qml_bridge.cpp",
                    "src/gui/qml/main_qml.cpp"):
    assert requirement not in cmake, requirement
qml=root/"src/gui/qml"
assert not list(qml.rglob("*.qml"))
assert not list(qml.rglob("*.js"))
assert not list(qml.rglob("*.qrc"))
for backend in ("uia_worker_main.cpp","visual_worker_main.cpp",
                "element_candidate_service.cpp","visual_tools_gateway.cpp"):
    assert (qml/backend).is_file(), backend
for legacy in ("main_window.cpp","sequence_editor.cpp","input_lab.cpp",
               "input_recorder_dialog.cpp","recording_review_dialog.cpp"):
    assert (root/"src/gui"/legacy).is_file(),legacy
print("Qt Widgets-only application and native worker services: PASS")
