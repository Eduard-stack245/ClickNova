#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Build a single-file AppImage after CMake/CTest succeeded on a Linux X11 builder.
# Requires separately installed linuxdeploy and linuxdeploy-plugin-qt tools.
# This script never installs a compiler or modifies system packages.
set -euo pipefail

(( $# >= 2 && $# <= 3 )) || {
  echo 'Usage: package-appimage-linux.sh <built-ClickNova-binary> <output-directory> [version]' >&2
  exit 2
}
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
exe="$(realpath -- "$1")"
output="$2"
version="${3:-0.6.0}"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid version' >&2; exit 2; }
[[ -f "$exe" && -x "$exe" && ! -L "$exe" ]] || { echo 'Missing executable ClickNova' >&2; exit 2; }
[[ "$(basename -- "$exe")" == ClickNova ]] || { echo 'Expected Linux ClickNova binary, not a Windows EXE' >&2; exit 2; }
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'This packaging recipe targets Linux x86-64' >&2; exit 2; }

# Prefer explicit absolute paths; otherwise find tools from PATH. Both tool
# names are official linuxdeploy release asset names, not random system tools.
deploy="${CLICKNOVA_LINUXDEPLOY:-linuxdeploy-x86_64.AppImage}"
plugin="${CLICKNOVA_LINUXDEPLOY_QT:-linuxdeploy-plugin-qt-x86_64.AppImage}"
command -v "$deploy" >/dev/null 2>&1 || [[ -x "$deploy" ]] || {
  echo 'linuxdeploy is missing; install the official linuxdeploy release tool on the BUILD machine' >&2; exit 3;
}
command -v "$plugin" >/dev/null 2>&1 || [[ -x "$plugin" ]] || {
  echo 'linuxdeploy Qt plugin is missing; install it on the BUILD machine' >&2; exit 3;
}
[[ -f "$root/packaging/clicknova.desktop" && -f "$root/packaging/clicknova.svg" ]] || {
  echo 'Missing desktop integration assets' >&2; exit 3;
}
# Do not package user data, private config files or pre-existing libraries.
[[ ! -e "$root/data" ]] || { echo 'Refusing to package source checkout with portable user data' >&2; exit 4; }

mkdir -p -- "$output"
output="$(cd -- "$output" && pwd -P)"
final="$output/ClickNova-${version}-Linux-x86_64.AppImage"
[[ ! -e "$final" ]] || { echo "Refusing to overwrite $final" >&2; exit 4; }
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT
appdir="$work/AppDir"
mkdir -p -- "$appdir/usr/bin/extensions" "$appdir/usr/share/doc/ClickNova"
cp -a -- "$root/extensions/chromium" "$root/extensions/firefox" "$appdir/usr/bin/extensions/"
cp -- "$root/LICENSE" "$appdir/usr/share/doc/ClickNova/LICENSE"
# Reuse the desktop app's existing brand mark as the AppImage icon.
# This is the same product identity, not a second launcher UI.
export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 VERSION="$version"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[[ -n "$QMAKE" ]] || { echo 'Qt 6 qmake must be available on the BUILD machine' >&2; exit 3; }
# linuxdeploy will copy linked shared libraries; explicitly include libXtst
# because the X11 backend loads it with dlopen (ldd alone cannot see it).
xtst="$(ldconfig -p 2>/dev/null | awk '/libXtst\.so\.6 \(libc6,x86-64\)/ {print $NF; exit}')"
[[ -f "$xtst" ]] || { echo 'libXtst.so.6 is required for Linux X11 playback' >&2; exit 3; }
(
  cd -- "$work"
  "$deploy" --appdir "$appdir" \
    --executable "$exe" \
    --library "$xtst" \
    --desktop-file "$root/packaging/clicknova.desktop" \
    --icon-file "$root/packaging/clicknova.svg" \
    --plugin qt --output appimage
)
mapfile -d '' found < <(find "$work" -maxdepth 1 -type f -name '*.AppImage' -print0)
[[ "${#found[@]}" -eq 1 && -s "${found[0]}" ]] || {
  echo 'Expected exactly one nonempty AppImage from linuxdeploy' >&2; exit 5;
}
mv -- "${found[0]}" "$final"
chmod +x "$final"
# AppImage must be an ELF runtime; reject accidental upload of source or a text launcher.
[[ "$(od -An -tx1 -N4 "$final" | tr -d ' \n')" == 7f454c46 ]] || {
  rm -f -- "$final"
  echo 'linuxdeploy produced a non-ELF artifact' >&2; exit 5;
}
echo "Created AppImage: $final"
echo 'AppImage is independent of Qt SDK/compiler; Linux kernel, glibc, X11 and desktop libraries still impose compatibility limits.'
