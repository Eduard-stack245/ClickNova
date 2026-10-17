# SPDX-License-Identifier: Apache-2.0
"""Exercise AppImage release staging without Qt or remote packaging downloads.

The real AppImage build is tested in the GitHub Linux release runner.
This test verifies our arguments, safe packaging inputs and release naming.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile
import shutil

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/package-appimage-linux.sh"
assert SCRIPT.is_file()

with tempfile.TemporaryDirectory(prefix="clicknova release ") as tmp:
    t = Path(tmp)
    exe = t / "ClickNova"
    exe.write_bytes(b"\x7fELFfake-linux-executable")
    exe.chmod(0o755)
    tools = t / "tools"
    tools.mkdir()
    plugin = tools / "linuxdeploy-plugin-qt-x86_64.AppImage"
    plugin.write_text("#!/bin/sh\nexit 0\n")
    plugin.chmod(0o755)
    deploy = tools / "linuxdeploy-x86_64.AppImage"
    deploy.write_text("""#!/bin/bash
set -euo pipefail
[[ \"${APPIMAGE_EXTRACT_AND_RUN:-}\" == 1 ]]
[[ \"${ARCH:-}\" == x86_64 ]]
[[ \"${VERSION:-}\" == 0.6.0 ]]
appdir=''
while (( $# )); do
  case \"$1\" in
    --appdir) appdir=$2; shift 2 ;;
    --desktop-file|--icon-file|--executable|--library) test -f \"$2\"; shift 2 ;;
    --plugin) [[ \"$2\" == qt ]]; shift 2 ;;
    --output) [[ \"$2\" == appimage ]]; shift 2 ;;
    *) echo \"unknown argument: $1\" >&2; exit 19 ;;
  esac
done
[[ -f \"$appdir/usr/bin/extensions/chromium/manifest.json\" ]]
[[ -f \"$appdir/usr/bin/extensions/firefox/manifest.json\" ]]
[[ -f \"$appdir/usr/share/doc/ClickNova/LICENSE\" ]]
printf '\\177ELFmock-bundled-AppImage' > ClickNova-mock-x86_64.AppImage
""")
    deploy.chmod(0o755)
    qmake = tools / "qmake6"
    qmake.write_text("#!/bin/sh\nexit 0\n")
    qmake.chmod(0o755)
    output = t / "release folder"
    env = dict(os.environ,
               QMAKE=str(qmake),
               CLICKNOVA_LINUXDEPLOY=str(deploy),
               CLICKNOVA_LINUXDEPLOY_QT=str(plugin))
    command = ["bash", str(SCRIPT), str(exe), str(output), "0.6.0"]
    first = subprocess.run(command, env=env, text=True, capture_output=True)
    assert first.returncode == 0, first.stdout + first.stderr
    bundle = output / "ClickNova-0.6.0-Linux-x86_64.AppImage"
    assert bundle.read_bytes() == b"\x7fELFmock-bundled-AppImage"
    second = subprocess.run(command, env=env, text=True, capture_output=True)
    assert second.returncode != 0, "A release must not silently overwrite previous output"
    assert bundle.read_bytes() == b"\x7fELFmock-bundled-AppImage"
    invalid = subprocess.run(command[:-1] + ["0.6.0/unsafe"], env=env, text=True, capture_output=True)
    assert invalid.returncode != 0
    # The removable release must contain ONLY declared own files, and its
    # uninstaller must preserve private user scenarios and pre-existing libs.
    archive = t / "bundle.zip"
    bundle_cmd = ["python3", str(ROOT / "scripts/package-appimage-bundle-linux.py"), str(bundle), str(archive)]
    packaged = subprocess.run(bundle_cmd, text=True, capture_output=True)
    assert packaged.returncode == 0, packaged.stderr + packaged.stdout
    with zipfile.ZipFile(archive) as z:
        names = set(z.namelist())
        assert f"ClickNova/{bundle.name}" in names
        assert "ClickNova/install-manifest.tsv" in names
        assert "ClickNova/uninstall-clicknova.sh" in names
        assert "ClickNova/README-PORTABLE.txt" in names
        z.extractall(t / "unpacked")
    extracted = t / "unpacked/ClickNova"
    (extracted / "foreign-library.so").write_bytes(b"PRE-EXISTING THIRD-PARTY LIB")
    user_data = extracted / "ClickNova-data"
    (user_data / "scenarios").mkdir(parents=True)
    (user_data / "scenarios/example.json").write_text("SAVED USER SCENARIO")
    uninst = subprocess.run(["bash", str(extracted / "uninstall-clicknova.sh"), "--confirmed",
                             "--data-dir", str(user_data)], text=True, capture_output=True)
    assert uninst.returncode == 0, uninst.stdout + uninst.stderr
    assert not (extracted / bundle.name).exists()
    assert (extracted / "foreign-library.so").read_bytes() == b"PRE-EXISTING THIRD-PARTY LIB"
    assert (user_data / "scenarios/example.json").read_text() == "SAVED USER SCENARIO"
    assert subprocess.run(bundle_cmd, text=True, capture_output=True).returncode != 0

print("ClickNova AppImage packaging contract passed")
