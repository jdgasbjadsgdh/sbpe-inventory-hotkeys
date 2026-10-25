#!/bin/bash
set -euo pipefail
PACKAGE_DIR="$(cd "$(dirname "$0")" && pwd -P)"
if [ "$(uname -s)" != Darwin ]; then printf '%s\n' 'Use the Windows or Web package on this system.'; exit 1; fi
TARGET_DIR="${1:-}"
if [ -z "$TARGET_DIR" ]; then
  TARGET_DIR="$(/usr/bin/osascript -e 'POSIX path of (choose folder with prompt "Choose your existing Mac SBPE folder (contains config.ini and sbpe-mac.dylib)")')" || exit 0
fi
TARGET_DIR="$(cd "$TARGET_DIR" && pwd -P)"
if [ "$TARGET_DIR" = "$PACKAGE_DIR" ]; then printf '%s\n' 'This folder is ready. Open RUN.command when you want to play.'; exit 0; fi
if [ ! -f "$TARGET_DIR/config.ini" ] || [ ! -f "$TARGET_DIR/sbpe-mac.dylib" ]; then
  printf '%s\n' 'That folder is not a Mac SBPE installation. Choose its main folder.'; exit 1
fi
cd "$PACKAGE_DIR"
/usr/bin/shasum -a 256 -c package.sha256 >/dev/null
BACKUP_DIR="$TARGET_DIR/loading-plugin-backups/$(date '+%Y%m%d-%H%M%S')-$$"
mkdir -p "$BACKUP_DIR"
FILES=(sbpe-mac.dylib RUN.command START_GAME.sh PLATFORM.sh FIND_GAME.sh PREPARE_RETINA.sh data/boosts.json data/items.json data/chest-rules.json config.ini)
NEW_FILES=()
for RELATIVE in "${FILES[@]}"; do
  if [ -f "$TARGET_DIR/$RELATIVE" ]; then
    mkdir -p "$BACKUP_DIR/$(dirname "$RELATIVE")"
    cp -p "$TARGET_DIR/$RELATIVE" "$BACKUP_DIR/$RELATIVE"
  else NEW_FILES+=("$RELATIVE"); fi
done
rollback() {
  local status=$?
  if [ "$status" -ne 0 ]; then
    for RELATIVE in "${FILES[@]}"; do
      if [ -f "$BACKUP_DIR/$RELATIVE" ]; then
        cp -p "$BACKUP_DIR/$RELATIVE" "$TARGET_DIR/$RELATIVE.loading-restore" && mv -f "$TARGET_DIR/$RELATIVE.loading-restore" "$TARGET_DIR/$RELATIVE" || true
      fi
    done
    for RELATIVE in "${NEW_FILES[@]}"; do rm -f "$TARGET_DIR/$RELATIVE"; done
    printf 'Installation failed; previous files restored. Backup: %s\n' "$BACKUP_DIR" >&2
  fi
  rm -f "$TARGET_DIR/config.ini.loading-new"
  rm -f "$TARGET_DIR/config.ini.emergency-new"
}
trap rollback EXIT
for RELATIVE in "${FILES[@]}"; do
  [ "$RELATIVE" = config.ini ] && continue
  mkdir -p "$TARGET_DIR/$(dirname "$RELATIVE")"
  cp -p "$PACKAGE_DIR/$RELATIVE" "$TARGET_DIR/$RELATIVE.loading-new"
  mv -f "$TARGET_DIR/$RELATIVE.loading-new" "$TARGET_DIR/$RELATIVE"
done
# Add only missing loading settings. Keep every existing control and option.
awk '
function defaults() {
  if (!seen["enabled"]) print "enabled = yes"
  if (!seen["mode"]) print "mode = manual"
  if (!seen["gui_visible"]) print "gui_visible = yes"
  if (!seen["stage_early_seconds"]) print "stage_early_seconds = 360"
  if (!seen["observe_spawns"]) print "observe_spawns = no"
  if (!seen["reconnect_on_release"]) print "reconnect_on_release = yes"
}
/^[ \t]*\[/ {
  if (inside) defaults()
  section=$0; gsub(/[ \t\r]/,"",section)
  inside=(section=="[plugin_loading_probe]")
  if (inside) found=1
}
inside && /^[ \t]*[^#;=]+=/ {
  key=$0; sub(/=.*/,"",key); gsub(/[ \t]/,"",key); seen[key]=1
}
{ print }
END {
  if (inside) defaults()
  if (!found) { print "\n[plugin_loading_probe]"; defaults() }
}' "$TARGET_DIR/config.ini" > "$TARGET_DIR/config.ini.loading-new"
if [ "${SBPE_INSTALL_EMERGENCY_HOLD:-0}" = 1 ]; then
  /usr/bin/awk -f "$PACKAGE_DIR/emergency-settings.awk" "$TARGET_DIR/config.ini.loading-new" > "$TARGET_DIR/config.ini.emergency-new"
  mv -f "$TARGET_DIR/config.ini.emergency-new" "$TARGET_DIR/config.ini.loading-new"
fi
chmod "$(stat -f '%Lp' "$TARGET_DIR/config.ini")" "$TARGET_DIR/config.ini.loading-new"
mv -f "$TARGET_DIR/config.ini.loading-new" "$TARGET_DIR/config.ini"
trap - EXIT
printf 'Loading controls installed. Settings preserved.\nBackup: %s\nOpen your usual RUN.command at your next chosen restart.\n' "$BACKUP_DIR"
if [ "${SBPE_INSTALL_EMERGENCY_HOLD:-0}" = 1 ]; then
  printf '%s\n' 'Emergency hold enabled. Shortcut: Controls > SBPE > Emergency hold.' 'F4 is assigned if available; existing assignments are preserved.'
fi
