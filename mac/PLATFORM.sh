#!/bin/bash
# Sourced by RUN.command. All game modules remain Intel code under Rosetta.
sbpe_mac_platform() {
  local machine="$1" apple_hardware="${2:-0}" translated="${3:-0}"
  case "$machine" in
    arm64) printf '%s\n' apple_silicon ;;
    x86_64)
      if [ "$apple_hardware" = 1 ] || [ "$translated" = 1 ]; then printf '%s\n' apple_silicon
      else printf '%s\n' intel; fi ;;
    *) return 1 ;;
  esac
}
sbpe_rosetta_available() {
  "${SBPE_ARCH_TOOL:-/usr/bin/arch}" -x86_64 "${SBPE_TRUE_TOOL:-/usr/bin/true}" >/dev/null 2>&1
}
sbpe_launch_game() {
  local platform="$1" mod_root="$2" executable="$3"
  if [ "$platform" = apple_silicon ]; then
    # Set DYLD variables inside the translated shell, after protected arch/bash.
    "${SBPE_ARCH_TOOL:-/usr/bin/arch}" -x86_64 "${SBPE_BASH_TOOL:-/bin/bash}" "$mod_root/START_GAME.sh" "$mod_root" "$executable"
  else
    "${SBPE_BASH_TOOL:-/bin/bash}" "$mod_root/START_GAME.sh" "$mod_root" "$executable"
  fi
}
