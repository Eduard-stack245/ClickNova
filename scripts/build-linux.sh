#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Linux Qt application build (X11 automation requires libXtst and a global F12 grab).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${CLICKNOVA_BUILD_DIR:-$root/build-linux}"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCLICKNOVA_BUILD_GUI=ON \
    -DCMAKE_INSTALL_PREFIX="$build/install" "$@"
cmake --build "$build" --parallel "${CLICKNOVA_JOBS:-2}"
ctest --test-dir "$build" --output-on-failure
cmake --install "$build"
printf '\nLinux Qt build installed at %s/install\n' "$build"
printf 'Note: X11 playback uses XTEST and F12 emergency stop. Wayland/AT-SPI, input recording and quick-click mode remain unsupported.\n'
