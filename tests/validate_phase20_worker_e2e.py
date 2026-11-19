# SPDX-License-Identifier: MIT
"""Source contract only; never masquerades as actual Qt/MSVC/UIA execution."""
from pathlib import Path
r = Path(__file__).resolve().parents[1]
package = r.parent
read = lambda p: p.read_text(encoding='utf-8')
protocol = read(r/'include/clicknova/uia_worker_protocol.hpp')
transport = read(r/'src/gui/qml/uia_isolated_transport.cpp')
worker = read(r/'src/gui/qml/uia_worker_main.cpp')
fixture = read(r/'tests/qml_uia_worker_e2e_tests.cpp')
patch = read(package/'scripts/apply-phase20.ps1')
runner = read(package/'scripts/prepare-and-build.ps1')
cli = read(r/'scripts/test-uia-worker-windows.ps1')
assert 'inline bool matchesRequest' in protocol
for rule in ['request.nonce', 'result.items.size() > request.limits.maxCandidates',
             'result.visited > request.limits.maxVisited',
             'result.items.size() > result.visited',
             'ca::sameIdentity(request.candidate, reply.verification.match)']:
    assert rule in protocol, rule
assert '!matchesRequest(request,reply)' in transport
assert 'std::cin.peek()' in worker
assert 'std::cin.read' in worker and 'std::cout.write' in worker
assert '--uia-worker' in fixture and 'invokeWorker(executable,verify)' in fixture
assert 'duplicated' not in fixture  # intentional unique ID proof without native global names
assert 'duplicateCreated.load()' in fixture and 'VerifyStatus::Ambiguous' in fixture
assert 'Status::UnsafeTarget' in fixture and 'Outcome::Cancelled' in fixture
assert 'framed.push_back(0x42)' in fixture and 'trailing.exitCode() != 0' in fixture
assert 'SendInput' in fixture and 'no SendInput' in fixture  # documentation comment only
assert 'add_test(NAME qml_uia_worker_e2e_tests' in patch
assert 'SKIP_RETURN_CODE 77' in patch and 'CLICKNOVA_UIA_INTERACTIVE_TESTS' in patch
assert "apply-phase20.ps1" in runner
assert 'Visual Studio 17 2022' in cli and '-Interactive' not in cli  # switch accepted as param; no implicit opt-in
assert 'Qt 6.12' in read(r/'docs/PHASE20_VALIDATION.md')
print('Phase20 worker IPC & opt-in Windows e2e source contract: OK (not a Windows execution)')
