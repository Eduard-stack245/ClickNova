# SPDX-License-Identifier: MIT
"""Static source contract; never claim this is a real Windows/JAB run."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
q=r/'src/gui/qml'
get=lambda rel: (r/rel).read_text(encoding='utf-8')
java=get('src/gui/qml/element_candidate_java_win32.cpp')
worker=get('src/gui/qml/uia_worker_main.cpp')
ipc=get('include/clicknova/uia_worker_protocol.hpp')
policy=get('include/clicknova/java_preview_policy.hpp')
service=get('src/gui/qml/element_candidate_service.cpp')
ui=get('src/gui/qml/StepsPage.qml')
translation=get('src/gui/qml/I18n.js')
installer=get('../scripts/prepare-and-build.ps1')
cmake=get('../scripts/apply-phase22.ps1')
assert 'createNativeJavaCandidateProvider()' in worker
assert 'request.provider == ipc::Provider::Java' in worker
assert 'createNativeMsaaCandidateProvider()' in worker
assert 'createNativeUiaCandidateProvider()' in worker
assert "request.provider != ipc::Provider::Uia" in worker
for marker in ['WindowsAccessBridge-64.dll', 'LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR',
               'GetWindowThreadProcessId', 'javaHomeDllPath()', 'systemDllPath()',
               'GetProcAddress', 'releaseJavaObject', 'PrivateInfo',
               'createNativeJavaCandidateProvider()', 'SecureZeroMemory',
               'validWindow(target)', 'limits.maxElapsedMs', 'limits.maxCandidates']:
    assert marker in java, marker
for forbidden in ['SendInput(', 'setTextContents', 'doAccessibleActions', 'getAccessibleTextRange',
                  'requestFocus(', 'GetProcAddress(library, "getAccessibleTextInfo"',
                  'getAccessibleContextWithFocus', 'accDoDefaultAction']:
    assert forbidden not in java, forbidden
assert 'password text' in policy
assert 'automationId.empty()' in policy and 'className.empty()' in policy
assert 'Provider::Msaa || reply.provider == Provider::Java' in ipc
assert 'Provider::Msaa || value.provider==Provider::Java' in ipc
assert 'Provider::Msaa || request.provider == Provider::Java' in ipc
assert '(o == Operation::Verify && p == Provider::Uia)' in ipc
assert 'if (providerKind_ != 0) return false;' in service
assert 'scanSelectedElementsWithProvider(clicknova.selectedAction, 2)' in ui
assert 'scanJava' in ui
assert '"scanJava": ["Java preview (read-only)", "Перегляд Java (лише читання)"]' in translation
assert "apply-phase22.ps1'" in installer
assert 'src/gui/qml/element_candidate_java_win32.cpp' in cmake
assert 'qml_java_preview_policy_tests' in cmake
assert 'qml_java_worker_protocol_tests' in cmake
assert 'qml_java_worker_e2e_tests' in cmake
assert 'SKIP_RETURN_CODE 77' in cmake
assert 'CLICKNOVA_JAVA_INTERACTIVE_TESTS' in cmake
assert (r/'scripts/test-java-worker-windows.ps1').is_file()
assert (r/'tests/fixtures/ClickNovaJavaPreviewFixture.java').is_file()
print('Phase22 static Java Access Bridge privacy, read-only IPC and build patch: PASS (not Windows integration)')
