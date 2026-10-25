# Inventory hotkeys for Windows SBPE

This standalone plugin equips bag items without opening inventory. It works
independently of the connectivity plugin and preserves other SBPE plugins.
Use Windows SBPE 1.7.0 with its bundled **32-bit PyPy** and supported native
StarBreak client. The game and SBPE base installation are not included.

## Install

1. Download and extract the entire Windows ZIP.
2. Double-click `INSTALL.cmd`. Paste the main SBPE folder path containing
   `manager.py`, `pypy`, and `plugins`, then press Enter. You can also drag that
   SBPE folder onto `INSTALL.cmd`.
3. Open **your SBPE folder → plugins → inventory_hotkeys.py** in a text editor.
4. Change the `SLOT_KEYS` block near the top. For example:

```python
SLOT_KEYS = {
    1: "Z", 2: "X", 3: "1",
    4: "", 5: "", 6: "",
    7: "", 8: "", 9: "",
    10: "", 11: "", 12: "",
}
```

5. Save the file. SBPE normally reloads changed Python plugins within a few
   seconds. If the game was closed, launch through your normal SBPE `run.bat`.

All keys start unassigned. `""` means unbound. Names are SDL key names such as
`"Z"`, `"1"`, `"F2"`. Modifier-only keys are rejected.
Choose keys that are unused by StarBreak and other plugins. This Windows
plugin polls input; it does not remove the game's own action for that key.
The installer backs up replaced files, preserves an existing `SLOT_KEYS`
block and leaves your `config.ini` and unrelated plugins unchanged.

## Use

Slot 1 is top-left in the bag. Count left to right, then down the next row.
There are 12 available bindings; only slots unlocked on your character work.
Put the intended item in that position, close inventory and tap its key.
The displaced equipped item returns to that bag position. Pressing again can
swap it back. Empty slots and items without a normal equipment slot do nothing.

Typing, unfocused windows, menus, dead/unspawned characters, active loading
holds and free camera block the action. Held keys do not repeatedly swap.
The plugin calls the native inventory handler, retaining class/slot checks,
normal input budget and server validation. It does not modify cooldowns.

## Troubleshooting

If nothing happens, verify the key block, window focus, a living spawned
character, a nonempty unlocked slot and no active inventory/menu/hold. Try
an unused key. A normal game restriction can reject an equip attempt.

For `native method` or `symbols missing` errors, check `remote.log`. This plugin
resolves exact C++ methods from the installed game and compares relocated
entry bytes before calling them. It disables itself on unsupported builds
instead of guessing addresses. It creates `inventory-hotkeys-target.json`
locally; do not copy that generated file between installations.

Set `[plugin_inventory_hotkeys] enabled = no` in `config.ini` to disable it,
or remove `plugins/inventory_hotkeys.py` while the game is closed.

Offline checks cover typed requests, cleanup, x86 thiscall adapters, exact
symbol resolution, input guards and installer/key preservation. A live Windows
gameplay test has not been performed. Includes pyelftools under its own license
in `inventory_hotkeys_vendor/LICENSE-pyelftools.txt`.
