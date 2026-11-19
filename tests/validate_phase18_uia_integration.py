# SPDX-License-Identifier: MIT
"""Static scope/integration guard; cannot substitute for the Windows UIA test."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
qml = root / 'src/gui/qml'
read = lambda p: p.read_text(encoding='utf-8')
win = read(qml / 'element_candidate_win32.cpp')
service = read(qml / 'element_candidate_service.cpp')
policy = read(root / 'include/clicknova/element_candidate_search.hpp')
fixture = read(root / 'tests/qml_uia_windows_integration_tests.cpp')
installer = read(root.parent / 'scripts/prepare-and-build.ps1')
ps = read(root.parent / 'scripts/apply-phase18.ps1')
assert 'context.alreadyScanning = false;' not in service
assert 'refusal_ = authorize(context);' in service
assert 'coverage.depthPruned = true' in win
assert 'coverage.foreignSubtree = true' in win
assert 'coverage.budgetExhausted = true' in win
assert 'r.truncated = !coverage.complete();' in win
assert 'verifySnapshot(candidate, live)' in win
assert 'live.status != Status::Complete || live.truncated' in policy
assert 'get_CurrentName' not in win and 'get_CurrentValue' not in win
assert 'Invoke(' not in win and 'SendInput(' not in win
assert 'TraversalCompleteness' in policy
assert 'VerifyStatus::Ambiguous' in fixture
assert 'VerifyStatus::UnsafeTarget' in fixture
assert 'VerifyStatus::Cancelled' in fixture
assert 'CLICKNOVA_UIA_INTERACTIVE_TESTS' in ps
assert 'TIMEOUT 45' in ps
assert 'apply-phase18.ps1' in installer
print('Phase18 source, completeness, Windows integration and installer contracts: OK')
