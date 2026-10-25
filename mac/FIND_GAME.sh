#!/bin/bash
# Bash 3.2 compatible: no account names, model IDs or display sizes hardcoded.
starbreak_executable_at() {
  local location="$1" candidate
  for candidate in "$location" \
    "$location/Contents/MacOS/mvmmoclient" \
    "$location/MVMMOClient.app/Contents/MacOS/mvmmoclient" \
    "$location/steamapps/common/StarBreak/MVMMOClient.app/Contents/MacOS/mvmmoclient" \
    "$location/common/StarBreak/MVMMOClient.app/Contents/MacOS/mvmmoclient"; do
    if [ -f "$candidate" ] && [ -x "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}
find_starbreak_game() {
  local steam_root="$1" preferred="${2:-}" library vdf
  if [ -n "$preferred" ]; then
    starbreak_executable_at "$preferred"
    return $?
  fi
  if starbreak_executable_at "$steam_root"; then return 0; fi
  for vdf in "$steam_root/steamapps/libraryfolders.vdf" "$steam_root/config/libraryfolders.vdf"; do
    [ -f "$vdf" ] || continue
    while IFS= read -r library; do
      if starbreak_executable_at "$library"; then return 0; fi
    done < <(awk -F '"' '
      $2 == "path" || ($2 ~ /^[0-9]+$/ && $4 ~ /^\//) {
        p=$4; gsub(/\\\\/, "\\", p); print p
      }' "$vdf")
  done
  return 1
}
