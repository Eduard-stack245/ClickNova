#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Only installed portable-package files and app-owned OCR models are removable.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
manifest="$root/install-manifest.tsv"
[[ -f "$manifest" && ! -L "$manifest" ]] || { echo 'ClickNova: no portable manifest; nothing removed.' >&2; exit 3; }
confirmed=0; parent=0; data="$root/data"
components=0; settings=0; scenarios=0; recovery=0; logs=0
while (($#)); do
  case "$1" in
    --confirmed) confirmed=1;;
    --parent-pid) shift; parent="$1";;
    --data-dir) shift; data="$1";;
    --remove-components) components=1;;
    --remove-settings) settings=1;;
    --remove-scenarios) scenarios=1;;
    --remove-recovery) recovery=1;;
    --remove-logs) logs=1;;
    *) echo "Unsupported argument: $1" >&2; exit 2;;
  esac
  shift
done
ask() { local answer; read -r -p "$1 [y/N] " answer; [[ "$answer" == y || "$answer" == Y ]]; }
if (( ! confirmed )); then
  ask 'Uninstall ClickNova?' || exit 0
  ask 'Remove ClickNova-downloaded OCR models?' && components=1 || :
  ask 'Delete settings?' && settings=1 || :
  ask 'Delete scenarios/autosave?' && scenarios=1 || :
  ask 'Delete recording recovery?' && recovery=1 || :
  ask 'Delete logs?' && logs=1 || :
fi
# Data root must be either under this package or a narrowly named per-user fallback.
case "$data" in
  "$root/data"|"$root/ClickNova-data") ;;
  "$HOME/.local/share/ClickNova"|"$HOME/.config/ClickNova"|"$HOME/.local/share/ClickNova/ClickNova") ;;
  *) echo 'Unsafe data location rejected' >&2; exit 4;;
esac
[[ ! -L "$data" ]] || { echo 'Symlinked data root rejected' >&2; exit 4; }
# Relative name must never escape base, and no component may be a symlink.
safe_path() {
  local base="$1" relative="$2" piece current="$1"
  [[ "$relative" && "$relative" != /* && "$relative" != *'\'* && "$relative" != *':'* ]] || return 1
  local -a parts; IFS='/' read -r -a parts <<< "$relative"
  for piece in "${parts[@]}"; do
    [[ "$piece" && "$piece" != '.' && "$piece" != '..' ]] || return 1
    current="$current/$piece"
    [[ ! -L "$current" ]] || return 1
  done
  printf '%s\n' "$current"
}
remove_verified() {
  local base="$1" hash="$2" relative="$3" target actual
  [[ "$hash" =~ ^[a-fA-F0-9]{64}$ ]] || return 1
  target="$(safe_path "$base" "$relative")" || return 1
  [[ -f "$target" ]] || return 1
  actual="$(sha256sum -- "$target" | cut -d' ' -f1)"
  [[ "$actual" == "$hash" ]] || { echo "Preserved modified file: $relative"; return 1; }
  rm -- "$target" && echo "Removed: $relative"
}
remove_data_dir() {
  local relative="$1" target
  target="$(safe_path "$data" "$relative")" || return 0
  [[ -d "$target" ]] || return 0
  # Refuse directories with symlinks: do not follow or remove user link targets.
  if find -P "$target" -type l -print -quit | grep -q .; then
    echo "Preserved data folder containing symbolic links: $relative"; return 0
  fi
  rm -r -- "$target"
}
if ((parent > 0)); then
  for ((i=0;i<60;i++)); do
    if ! kill -0 "$parent" 2>/dev/null; then break; fi
    sleep 1
  done
  if kill -0 "$parent" 2>/dev/null; then echo 'ClickNova still running' >&2; exit 6; fi
fi
if ((components)) && [[ -f "$data/owned-components.tsv" && ! -L "$data/owned-components.tsv" ]]; then
  tmp="$(mktemp "$data/.owned-components.XXXXXXXX")"
  while IFS=$'\t' read -r hash relative; do
    [[ "$hash" ]] || continue
    if [[ "$relative" == tessdata/eng.traineddata || "$relative" == tessdata/rus.traineddata ]]; then
      remove_verified "$data" "$hash" "$relative" || printf '%s\t%s\n' "$hash" "$relative" >> "$tmp"
    else
      printf '%s\t%s\n' "$hash" "$relative" >> "$tmp"
    fi
  done < "$data/owned-components.tsv"
  if [[ -s "$tmp" ]]; then mv -- "$tmp" "$data/owned-components.tsv"; else rm -- "$tmp" "$data/owned-components.tsv"; fi
fi
(( ! settings )) || remove_data_dir config
if ((scenarios)); then
  remove_data_dir scenarios
  # Image templates are scenario-local assets and are kept if the user
  # chooses to preserve scenarios during uninstall.
  remove_data_dir templates
  draft="$(safe_path "$data" 'clicknova-steps-autosave.json')" || draft=''
  if [[ "$draft" && -f "$draft" ]]; then rm -- "$draft"; fi
fi
(( ! recovery )) || remove_data_dir recording-recovery
(( ! logs )) || remove_data_dir activity-logs
failed=0
while IFS=$'\t' read -r hash relative; do
  [[ -z "$hash" || "${hash:0:1}" == '#' ]] && continue
  target="$(safe_path "$root" "$relative")" || { failed=$((failed+1)); continue; }
  if [[ -f "$target" ]]; then remove_verified "$root" "$hash" "$relative" || failed=$((failed+1)); fi
done < "$manifest"
if ((failed)); then echo "$failed packaged files preserved; inspect manually." >&2; exit 5; fi
# Only delete empty directories; no user-created file is deleted.
# Attempt only directory ancestors of installed files, never traverse user data.
while IFS=$'\t' read -r hash relative; do
  [[ -z "$hash" || "${hash:0:1}" == '#' ]] && continue
  target="$(safe_path "$root" "$relative")" || continue
  dir="$(dirname -- "$target")"
  while [[ "$dir" != "$root" && "$dir" == "$root/"* ]]; do
    rmdir -- "$dir" 2>/dev/null || :
    dir="$(dirname -- "$dir")"
  done
done < "$manifest"
rm -- "$manifest"
rm -f -- "$root/Uninstall-ClickNova.cmd" "$root/uninstall-clicknova.ps1"
echo 'ClickNova removed; unselected data and pre-existing libraries preserved.'
rm -f -- "${BASH_SOURCE[0]}"
