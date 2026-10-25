#!/bin/bash
# Sourced by RUN.command. Prepare a separate app bundle; Steam's app is untouched.
prepare_retina_bundle() {
  local source_app="$1" destination_root="$2" expected_hash="$3"
  local destination="$destination_root/SBPE-StarBreak-Retina.app"
  local stamp="$destination_root/source.txt" plist
  local ditto_tool="${SBPE_DITTO_TOOL:-/usr/bin/ditto}"
  local plist_tool="${SBPE_PLIST_TOOL:-/usr/libexec/PlistBuddy}"
  if [ ! -f "$source_app/Contents/Info.plist" ]; then
    printf '%s\n' 'The game app Info.plist is missing; Retina setup cannot continue.' >&2
    return 1
  fi
  mkdir -p "$destination_root"
  if [ ! -x "$destination/Contents/MacOS/mvmmoclient" ] || [ ! -f "$stamp" ] || [ "$(cat "$stamp")" != "$source_app|$expected_hash" ]; then
    printf '%s\n' 'Preparing a separate Retina-enabled game copy (first launch may take a moment).' >&2
    "$ditto_tool" "$source_app" "$destination" || return 1
  fi
  plist="$destination/Contents/Info.plist"
  "$plist_tool" -c 'Delete :NSHighResolutionCapable' "$plist" >/dev/null 2>&1 || true
  "$plist_tool" -c 'Add :NSHighResolutionCapable bool true' "$plist" || return 1
  # A separate identity avoids inheriting the original app's low-resolution preference.
  "$plist_tool" -c 'Delete :CFBundleIdentifier' "$plist" >/dev/null 2>&1 || true
  "$plist_tool" -c 'Add :CFBundleIdentifier string local.sbpe.starbreak.retina' "$plist" || return 1
  if [ "$("$plist_tool" -c 'Print :NSHighResolutionCapable' "$plist")" != true ]; then
    printf '%s\n' 'Could not enable Retina in the copied app.' >&2
    return 1
  fi
  printf '%s\n' "$source_app|$expected_hash" > "$stamp"
  printf '%s\n' "$destination/Contents/MacOS/mvmmoclient"
}
