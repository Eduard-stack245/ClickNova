#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Package an ALREADY BUILT portable Linux directory. Does not vendor Linux deps.
set -euo pipefail
(( $# == 2 )) || { echo "Usage: $0 <ready-bundle-directory> <output-directory>" >&2; exit 2; }
source_dir="$(cd -- "$1" && pwd -P)"
target="$2"
[[ -f "$source_dir/ClickNova" ]] || { echo 'Missing Linux ClickNova binary' >&2; exit 2; }
[[ ! -e "$source_dir/data" ]] || { echo 'Refusing to package user data; use a clean Linux runtime directory' >&2; exit 2; }
if find -P "$source_dir" -type l -print -quit | grep -q .; then
  echo 'Symlinks in Linux package are not supported by the safe uninstall manifest' >&2
  exit 2
fi
[[ ! -e "$target" ]] || { echo 'Output folder already exists; refusing overwrite' >&2; exit 2; }
mkdir -p -- "$target"
cp -a -- "$source_dir/." "$target/"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cp -- "$script_dir/uninstall-clicknova.sh" "$target/uninstall-clicknova.sh"
chmod +x "$target/uninstall-clicknova.sh"
manifest="$target/install-manifest.tsv"
{
  printf '# ClickNova portable manifest v1\n'
  while IFS= read -r -d '' path; do
    relative="${path#"$target/"}"
    [[ "$relative" != 'install-manifest.tsv' && "$relative" != 'uninstall-clicknova.sh' ]] || continue
    [[ "$relative" != *$'\t'* && "$relative" != *$'\n'* ]] || { echo 'Unsafe filename' >&2; exit 3; }
    sha256sum -- "$path" | cut -d' ' -f1 | tr -d '\n'
    printf '\t%s\n' "$relative"
  done < <(find -P "$target" -type f -print0)
} > "$manifest"
echo "Linux portable directory: $target"
echo 'Note: runtime dependencies and Linux automation support are NOT provided by this packaging script.'
