# SBPE inventory hotkeys

Equip an item from a bag slot with a key, while inventory stays closed.
Mac SBPE, Windows SBPE and WebBreak versions are included, with source code
and installation/keybinding tutorials.

| Platform | Download | Instructions |
| --- | --- | --- |
| Mac | [Mac ZIP](downloads/SBPE-Inventory-Hotkeys-Mac-2026-10-25.zip?raw=true) | [Mac README](mac/README.md) |
| Windows | [Windows ZIP](downloads/SBPE-Inventory-Hotkeys-Windows-2026-10-25.zip?raw=true) | [Windows README](windows/README.md) |
| WebBreak | [Web ZIP](downloads/SBPE-Inventory-Hotkeys-Web-2026-10-25.zip?raw=true) | [Web README](web/README.md) |

Download the appropriate platform ZIP and extract its complete contents before
installing. Checksums are in [downloads/SHA256SUMS.txt](downloads/SHA256SUMS.txt).
The Mac archive contains a full native SBPE update. The Windows plugin is
standalone. The Web installer updates an existing override while retaining
other plugins, or starts from the included pinned WebBreak base.

## Quick tutorial

1. Install using your platform's README.
2. Assign a key for **inventory slot 1**. New inventory keys start unassigned;
   existing game and SBPE controls are preserved.
3. Put the intended item in the top-left bag slot and close inventory.
4. Focus the game and tap the key. The item equips into its normal equipment
   slot, returning the displaced item to the selected bag position.
5. Assign more slot keys if desired. Count left to right across three columns,
   then continue on the next row. There are 12 bindings; only unlocked slots work.

Pressing a slot key again can swap the previous equipment back. The key follows
the bag position, not an item name. Holding a key does not continuously swap.

## Where to change bindings

- **Mac:** Settings → Controls → SBPE → Next → **Equip bag slot 1–12**. For
  source defaults, edit the marked `INVENTORY HOTKEY DEFAULTS` block in
  [`mac/src/controls_config.h`](mac/src/controls_config.h), then rebuild.
  Saved menu/INI choices take precedence over source defaults.
- **Windows:** edit `SLOT_KEYS` at the top of
  [`windows/plugins/inventory_hotkeys.py`](windows/plugins/inventory_hotkeys.py).
  Edit the installed copy in your SBPE folder. Example: `1: "Z", 2: "X"`.
- **Web:** edit `SLOT_KEYS` near `SBPE INVENTORY HOTKEYS` in your installed
  Overrides client. Example: `1: "KeyZ", 2: "KeyX"`. The standalone source is
  [`web/inventory-hotkeys.js`](web/inventory-hotkeys.js). Save and reload when ready.

Each platform README gives complete code examples and update instructions.
Installers back up replacements and preserve existing key choices. They do not
start or restart your games.

## Behavior and compatibility

Each version uses the game's ordinary inventory handler. Empty/locked positions
and nonequippable items do nothing. Normal class restrictions, equipment types,
native input budget and server validation remain in effect. A swap attempt can
be rejected by game rules. The plugin does not alter weapon cooldowns or create
items.

Typing, menus, dead or unspawned characters, loading holds, recovery and free
camera block the action. Web actions are queued for the next game update and
cancelled if the player/room/connection changes. Windows polls keys, so choose
keys unused by the game's own controls as well as other plugins.

Supported targets are the pinned Intel Mac client, Windows SBPE 1.7.0 with its
bundled 32-bit PyPy, and WebBreak build 1574197275. Native method/ABI checks fail
closed on unsupported builds. The original Steam/game installation is required.

The Mac bundle also includes prior connectivity/chest guards and a new preflight
for the known missing-spawn native abort. That guard requests normal recovery;
it cannot restore a server-side spawn or guarantee staying in the mission.
resources belong to Crunchy Games.
