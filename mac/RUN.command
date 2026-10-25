#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd -P)"
cd "$ROOT"
exec > >(tee "$ROOT/launch.log") 2>&1
trap 'code=$?; if [ "$code" -ne 0 ]; then printf "\nLaunch stopped (code %s). Send launch.log and sbpe-mac.log back in the conversation.\n" "$code"; fi' EXIT
printf '%s\n' 'SBPE Mac 0.19 — Windows gameplay, DPS and total damage'
if [ "$(uname -s)" != Darwin ]; then
  printf '%s\n' 'This package requires macOS.'; exit 1
fi
source "$ROOT/PLATFORM.sh"
MACHINE="$(uname -m)"
ARM_HARDWARE="$(/usr/sbin/sysctl -n hw.optional.arm64 2>/dev/null || printf 0)"
TRANSLATED="$(/usr/sbin/sysctl -n sysctl.proc_translated 2>/dev/null || printf 0)"
if ! PLATFORM="$(sbpe_mac_platform "$MACHINE" "$ARM_HARDWARE" "$TRANSLATED")"; then
  printf '%s\n' 'This Mac architecture is not supported.'; exit 1
fi
if [ "$PLATFORM" = apple_silicon ]; then
  printf '%s\n' 'Apple silicon detected. The Intel game and mod will run together through Rosetta.'
  if ! sbpe_rosetta_available; then
    printf '%s\n' 'Rosetta could not run an Intel process on this Mac.' \
      'First open normal StarBreak from Steam and follow any macOS Rosetta installation prompt.' \
      'Alternatively run softwareupdate --install-rosetta in Terminal and follow the prompts.' \
      'Then quit StarBreak and run RUN.command again.'
    exit 1
  fi
fi
OS_VERSION="$(/usr/bin/sw_vers -productVersion)"
OS_MAJOR="${OS_VERSION%%.*}"
OS_REST="${OS_VERSION#*.}"
OS_MINOR="${OS_REST%%.*}"
if [ "$OS_MAJOR" -lt 10 ] || { [ "$OS_MAJOR" -eq 10 ] && [ "$OS_MINOR" -lt 14 ]; }; then
  printf '%s\n' 'This build requires macOS 10.14 or later.'; exit 1
fi
printf 'System: macOS %s, platform %s, game architecture x86_64\n' "$OS_VERSION" "$PLATFORM"
source "$ROOT/FIND_GAME.sh"
STEAM_ROOT="$HOME/Library/Application Support/Steam"
if ! GAME_EXE="$(find_starbreak_game "$STEAM_ROOT" "${1:-${SBPE_GAME_DIR:-}}")"; then
  printf '%s\n' 'StarBreak was not found in your Steam libraries.' \
    'Type bash and a space in Terminal, drag RUN.command in, add a space,' \
    'then drag your StarBreak folder or MVMMOClient.app in and press Return.'
  exit 1
fi
if [ -n "${DYLD_INSERT_LIBRARIES:-}" ]; then
  printf '%s\n' 'Another injection library is already configured in this Terminal. Open a fresh Terminal and try again.'; exit 1
fi
if /usr/bin/pgrep -x mvmmoclient >/dev/null 2>&1; then
  printf '%s\n' 'Quit the existing StarBreak game before running this launcher.'; exit 1
fi
if [ ! -f "$ROOT/sbpe-mac.dylib" ]; then
  printf '%s\n' 'sbpe-mac.dylib is missing. Extract the entire ZIP into one folder.'; exit 1
fi
GAME_HASH="$(/usr/bin/shasum -a 256 "$GAME_EXE")"
GAME_HASH="${GAME_HASH%% *}"
if [ "$GAME_HASH" != 8ab9adf4a5900a18532f3403bd07f9515fb59f4afbc5e8ebdb795aeeaf698d57 ]; then
  printf '%s\n' 'This StarBreak executable is not a supported game build.' \
    'The launcher has stopped before loading the mod. A different game build needs matching hook addresses.'; exit 1
fi
if [ ! -f "$ROOT/config.ini" ]; then
  printf '%s\n' 'config.ini is missing. Extract the entire ZIP into one folder.'; exit 1
fi
# Apply the same file-specific allowance used successfully for beta 0.1.
if /usr/bin/xattr -p com.apple.quarantine "$ROOT/sbpe-mac.dylib" >/dev/null 2>&1; then
  printf '%s\n' 'Allowing the downloaded SBPE library for this local test.'
  /usr/bin/xattr -d com.apple.quarantine "$ROOT/sbpe-mac.dylib"
fi
printf '%s\n' 'Game checksum matched. Starting StarBreak. Steam should already be running.' \
  'Windows gameplay overlays, DPS and total damage are included. V zoom, R hide, hold C history.'
ORIGINAL_GAME_EXE="$GAME_EXE"
GAME_APP="$(dirname "$(dirname "$(dirname "$GAME_EXE")")")"
source "$ROOT/PREPARE_RETINA.sh"
GAME_EXE="$(prepare_retina_bundle "$GAME_APP" "$ROOT/retina-runtime" "$GAME_HASH")"
COPIED_HASH="$(/usr/bin/shasum -a 256 "$GAME_EXE")"
COPIED_HASH="${COPIED_HASH%% *}"
if [ "$COPIED_HASH" != "$GAME_HASH" ]; then
  printf '%s\n' 'Copied executable checksum mismatch. Remove the retina-runtime folder and retry.'; exit 1
fi
printf '%s\n' 'Retina-capable app bundle prepared. Launching the separate copy.'
# Retain the installed game's working directory for any external resource paths.
cd "$(dirname "$ORIGINAL_GAME_EXE")"
sbpe_launch_game "$PLATFORM" "$ROOT" "$GAME_EXE"
