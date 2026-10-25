# Inventory hotkeys for WebBreak

Equip a bag item with a key while inventory stays closed. This requires the
WebBreak client **1574197275** by [pjrje4](https://github.com/pjrje4/WebBreak).
The inventory extension is independent of loading holds.

## Install or update

1. Extract the complete Web ZIP and open `INSTALL.html` in desktop Chrome or Edge.
2. In your StarBreak tab, open DevTools → **Sources → Overrides**, choose an
   Overrides folder and grant access. If WebBreak is already installed, use
   its existing Overrides folder.
3. In the installer, choose that folder, then click **Install / update**.
4. Reload StarBreak when you are ready. Keep DevTools open for Overrides.

The installer preserves existing WebBreak settings, other plugins, and an
existing inventory key block. It backs up the previous override before writing.
It does not reload your game. If installing into a new empty Overrides folder,
the pinned WebBreak base is included. This standalone base does not add loading
controls; updating an existing connectivity override preserves them.

The native export adapter is required: pasting only the plugin JavaScript
into an old WebBreak file is insufficient. Unsupported native functions cause
installation to stop without changing the file.

## Change keys in the plugin code

Open the installed Overrides file:
`www.starbreak.com/VR1574197275.mvmmoclient.js`.
Search for **SBPE INVENTORY HOTKEYS**, then edit `SLOT_KEYS` near that plugin's
start. This is the installed override, not the standalone source file.

```javascript
var SLOT_KEYS = {
  1: "KeyZ", 2: "KeyX", 3: "Digit1",
  4: "", 5: "", 6: "",
  7: "", 8: "", 9: "",
  10: "", 11: "", 12: ""
};
```

Save the override and reload the game when ready. All slots initially use `""`
(unbound). Web keys use browser **code names**: `KeyZ`, `Digit1`, `F2`, etc.
They refer to physical keyboard positions. Choose unused keys and give each
slot a different key. Loading F8/F9/F10 should remain with the loading plugin
if you use it. The installer preserves this block on updates.

## Use and behavior

Slot 1 is top-left in the bag; count across the three columns and then down.
Tap the chosen key while the game is focused and your character is spawned.
The item moves to its usual equipment slot, swapping the equipped item back
into that bag position. Only unlocked, nonempty positions work.

The action is queued until the game's next update. A changed room, connection
or player cancels it. Typing, menus, modifier combinations, free camera, loading
holds, death and lost focus block activation. A held key performs one attempt.

The adapter exports existing game functions and calls the normal native equip
handler. Compatibility, native input budget and server checks remain in place.
It does not alter cooldowns or implement raw packet injection.

To disable, leave all inventory keys blank or set
`[plugin_inventory_hotkeys] enabled = no` in WebBreak's embedded configuration,
then reload. Existing WebBreak and connectivity keys are not changed.

Offline browser tests use real WebBreak views/DOM and the pinned native item,
slot and inventory-handler functions. They cover accepted swaps, budget
rejection, context guards and installer preservation. A live server gameplay
trial has not been performed. WebBreak/game attribution remains in the
included upstream file; the original game resources remain with their owners.

## Rebuild from source

The ready-to-install Web ZIP includes payload-data.js. A repository source
checkout omits that large generated file. For a source build, obtain the
unedited WebBreak client from pinned commit
`ec7fe74579ec6ad7a5e663a2a55672de62fc8361`, edit this folder's
`inventory-hotkeys.js`, then run `python3 BUILD.py /path/to/VR1574197275.mvmmoclient.js`
with Node.js installed. The build validates the upstream SHA and native
function signatures, then creates payload-data.js and the combined override.
It does not install or reload a browser.
