#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Wrap a tested AppImage in a removable, ownership-tracked portable ZIP.

Only known build artifacts are copied. Never scan a developer's home directory.
The unpacked ZIP allows the existing safe uninstaller to verify hashes and
preserve foreign files and unselected ClickNova-data.
"""
import hashlib
from pathlib import Path
import re
import stat
import sys
import zipfile


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("Usage: package-appimage-bundle-linux.py <AppImage> <output.zip>")
    appimage = Path(sys.argv[1]).absolute()
    output = Path(sys.argv[2]).absolute()
    m = re.fullmatch(r"ClickNova-([0-9]+\.[0-9]+\.[0-9]+)-Linux-x86_64\.AppImage", appimage.name)
    if not m or not appimage.is_file() or appimage.is_symlink() or appimage.stat().st_size < 4:
        raise SystemExit("Expected regular ClickNova Linux x86-64 AppImage")
    with appimage.open("rb") as f:
        if f.read(4) != b"\x7fELF":
            raise SystemExit("AppImage is not an ELF executable")
    root = Path(__file__).resolve().parents[1]
    script = root / "scripts/uninstall-clicknova.sh"
    license_file = root / "LICENSE"
    if output.exists():
        raise SystemExit("Refusing to overwrite existing release ZIP")
    docs = (
        "ClickNova portable Linux x86-64 (X11)\n\n"
        "1. Extract this ZIP into a writable folder, e.g. ~/Applications/ClickNova.\n"
        "2. Run: chmod +x ClickNova-*-Linux-x86_64.AppImage\n"
        "3. Start the AppImage by double-click or from a terminal.\n"
        "4. ClickNova-data (settings/scenarios/OCR) stays next to the AppImage.\n"
        "5. To uninstall, run: bash uninstall-clicknova.sh\n"
        "   Choose the data categories to delete. Pre-existing files are preserved.\n\n"
        "End users do NOT need Qt, CMake, GCC, Clang, or source code.\n"
        "Linux X11 runtime with glibc 2.39+ (e.g. Ubuntu 24.04) is required;\n"
        "some system graphics/desktop dependencies may still be needed.\n"
        "Wayland automation is not supported. OpenCV/Tesseract engines are not\n"
        "present unless explicitly enabled when the release was built.\n"
    ).encode("utf-8")
    sources = {
        appimage.name: appimage.read_bytes(),
        "LICENSE": license_file.read_bytes(),
        "README-PORTABLE.txt": docs,
    }
    manifest = "# ClickNova portable manifest v1\n" + "".join(
        hashlib.sha256(data).hexdigest() + "\t" + name + "\n"
        for name, data in sources.items()
    )
    sources["install-manifest.tsv"] = manifest.encode("ascii")
    sources["uninstall-clicknova.sh"] = script.read_bytes()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as zipf:
        for name, data in sources.items():
            filename = "ClickNova/" + name
            info = zipfile.ZipInfo(filename)
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | (0o755 if name == appimage.name or name.endswith(".sh") else 0o644)) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            zipf.writestr(info, data)
    with zipfile.ZipFile(output) as verify:
        names = verify.namelist()
        if len(names) != len(sources) or any(verify.read("ClickNova/" + n) != data for n, data in sources.items()):
            raise RuntimeError("Portable bundle verification failed")
    print(f"Created Linux portable ZIP with ownership ledger: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
