#!/bin/bash
set -euo pipefail
if [ "$#" -ne 2 ]; then
  printf '%s\n' 'Launch this package with RUN.command.' >&2
  exit 1
fi
SBPE_ROOT="$1"
GAME_EXE="$2"
if [ ! -f "$SBPE_ROOT/sbpe-mac.dylib" ] || [ ! -x "$GAME_EXE" ]; then
  printf '%s\n' 'Game executable or SBPE library is missing.' >&2
  exit 1
fi
# The parent selected x86_64 before reaching this script on Apple silicon.
# Apply DYLD only to the game; log/backup helpers never load SBPE themselves.
auto_restart="${SBPE_AUTO_RESTART:-}"
if [ -z "$auto_restart" ]; then
  auto_restart="$(awk '
    /^[ \t]*\[/ {s=tolower($0); gsub(/[ \t\r]/,"",s)}
    s=="[general]" && /^[ \t]*auto_restart[ \t]*=/ {sub(/^[^=]*=/,""); gsub(/[ \t\r]/,""); print tolower($0); exit}
  ' "$SBPE_ROOT/config.ini" 2>/dev/null || true)"
  auto_restart="${auto_restart:-yes}"
fi
case "$auto_restart" in 0|no|false|off) auto_restart=0 ;; *) auto_restart=1 ;; esac
stopping=0
game_pid=""
stop_game() {
  stopping=1
  if [ -n "$game_pid" ]; then kill -TERM "$game_pid" 2>/dev/null || true; fi
}
trap stop_game INT TERM HUP
if [ "$auto_restart" = 1 ]; then printf '%s\n' 'Automatic crash relaunch enabled. Quit normally or press Ctrl-C here to stop.'; fi
while :; do
  if [ "$stopping" = 1 ]; then exit 0; fi
  SBPE_ROOT="$SBPE_ROOT" SDL_VIDEO_HIGHDPI_DISABLED=0 DYLD_INSERT_LIBRARIES="$SBPE_ROOT/sbpe-mac.dylib" "$GAME_EXE" &
  game_pid=$!
  if wait "$game_pid"; then game_status=0; else game_status=$?; fi
  if [ "$stopping" = 1 ]; then wait "$game_pid" 2>/dev/null || true; exit 0; fi
  game_pid=""
  if [ "$game_status" -eq 0 ]; then exit 0; fi
  # SIGINT/SIGTERM are intentional stop requests, even if sent to the game.
  if [ "$game_status" -eq 130 ] || [ "$game_status" -eq 143 ]; then exit 0; fi
  if [ "$auto_restart" != 1 ]; then exit "$game_status"; fi
  crash_stamp="$(date '+%Y%m%d-%H%M%S')"
  crash_dir="$SBPE_ROOT/crash-logs/$crash_stamp-$$"
  if mkdir -p "$crash_dir"; then
    for crash_log in sbpe-mac.log launch.log; do
      if [ -f "$SBPE_ROOT/$crash_log" ]; then cp "$SBPE_ROOT/$crash_log" "$crash_dir/$crash_log" || true; fi
    done
    printf 'Exit code: %s\n' "$game_status" > "$crash_dir/exit.txt"
  fi
  printf '\nStarBreak exited unexpectedly (code %s). Reopening with SBPE in 1 second.\n' "$game_status"
  sleep 1 &
  wait "$!" || true
  if [ "$stopping" = 1 ]; then exit 0; fi
done
