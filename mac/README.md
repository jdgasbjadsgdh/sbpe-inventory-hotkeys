# Inventory hotkeys for Mac SBPE

Equip an item from your bag without opening inventory. There are 12 independent
actions. Slot 1 is the top-left bag slot; count left to right across three
columns, then continue on the next row. Locked and empty slots do nothing.
The displaced equipped item returns to that bag slot, so pressing the key
again can swap it back. Keys refer to positions, not a remembered item name.

## Install

Download and extract the complete Mac ZIP. Double-click `INSTALL.command` and
choose your existing Mac SBPE folder. It backs up replaced files and preserves
your existing controls/settings. It does not restart the game. Activate the
native update on your next launch through your usual SBPE launcher.

For a fresh setup, put the extracted folder somewhere permanent, then use
`RUN.command` with the supported Mac Steam StarBreak installation. This bundle
supports the Intel game build with SHA-256
`8ab9adf4a5900a18532f3403bd07f9515fb59f4afbc5e8ebdb795aeeaf698d57`;
Apple silicon requires Rosetta. The original game and Steam are not included.

## Change keys in the game

1. Open **Settings → Controls → SBPE**.
2. Use **Next** at the bottom to find **Equip bag slot 1**, **Equip bag slot 2**,
   and the remaining inventory actions. There are three pages overall.
3. Click a Primary, Secondary, or Tertiary key cell and press your chosen key.
4. Backspace clears a binding; Escape cancels capture. Changes save to
   `config.ini`. The three key columns are alternatives for one action.

New inventory actions start unbound. Existing SBPE and game bindings are kept.
Choose an unused key. Loading keys F8/F9/F10 and modifier-only keys are reserved.

## Change defaults in the plugin source

Open `src/controls_config.h` in a text editor and find
`INVENTORY HOTKEY DEFAULTS`. Change the quoted second argument:

```c
INVENTORY_CONTROL(1,"Z"),INVENTORY_CONTROL(2,"X"),INVENTORY_CONTROL(3,"None"),
```

`"None"` means unbound. Use SDL names such as `"Z"`, `"1"`, or `"F2"`.
Save, then run `BUILD.command` on a Mac with Apple Command Line Tools installed.
Launch the rebuilt SBPE on your next restart. Source edits need a rebuild
because this Mac SBPE is native C, rather than a Python plugin host.

Saved menu bindings override source defaults. To change an existing installation,
the menu is easiest. You can also edit `[sbpe_controls]` in `config.ini`:

```ini
inventory_slot_1_primary = Z
inventory_slot_2_primary = X
```

Use F5 to reload an edited INI. Do not use **Reset to defaults** merely to
change inventory keys: that resets other SBPE controls too.

## How it works and limits

The hotkey reads the item in the chosen bag position and uses its normal
equipment type. It sends the same native inventory action used by the game UI.
Compatibility, unlocked slots, native input budget, and server validation still
apply. It does not change cooldowns, duplicate items, or bypass class restrictions.

Keys are ignored while typing, rebinding controls, paused, holding a load,
reconnecting, using free camera, dead, or without a spawned character. A key
press performs one attempt; holding it does not continuously swap. A request
can be rejected by normal game rules, so verify the equipped icon after use.

This full Mac SBPE update also includes the existing connectivity controls,
Hide all UI and chest-overlay guard. A new missing-spawn preflight avoids the
known `spawnObj != nullptr` native abort by rejecting that load before world
construction and requesting normal recovery after dispatch. It cannot create
a server spawn point or guarantee mission retention.

Offline native input, memory-safety, controls persistence, missing-spawn/hold
recovery and binary compatibility checks passed. Live inventory swapping still
needs confirmation after startup activation.
