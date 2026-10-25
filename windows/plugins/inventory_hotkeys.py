"""Equip a bag item without opening inventory. Windows SBPE 1.7.0."""

# EDIT KEYS HERE. Slot 1 is top-left; count across each row, then down.
# Use SDL key names, e.g. "Z", "X", "1", "F2". "" leaves a slot unbound.
# Use keys that are not already assigned to game actions or other plugins.
# Saving this file makes SBPE reload it automatically. No game restart needed.
SLOT_KEYS = {
    1: "", 2: "", 3: "",
    4: "", 5: "", 6: "",
    7: "", 8: "", 9: "",
    10: "", 11: "", 12: "",
}

import logging
from _remote import ffi, lib
from manager import PluginBase
import util
from inventory_hotkeys_native import NativeInventory


class Plugin(PluginBase):
    def onInit(self):
        self.native = NativeInventory(self.refs, ffi)
        self.binds = {}
        for slot, name in sorted(SLOT_KEYS.items()):
            if not name:
                continue
            scan = lib.SDL_GetScancodeFromName(name.encode('utf-8'))
            if not 1 <= slot <= 12 or not scan or 224 <= scan <= 231:
                raise ValueError('Invalid inventory slot/key: {} / {}'.format(slot, name))
            if scan in self.binds:
                raise ValueError('Inventory hotkeys must use different keys')
            self.binds[scan] = slot-1
        self.old = self.held()
        logging.info('Inventory hotkeys ready: %d assigned bag slots', len(self.binds))

    @staticmethod
    def held():
        count = ffi.new('int *')
        keys = lib.SDL_GetKeyboardState(count)
        return {i for i in range(count[0]) if keys[i]}

    def allowed(self, held):
        if held & {224, 226, 227, 228, 230, 231} or lib.SDL_IsTextInputActive():
            return False
        stage = self.refs.stage[0]
        if stage == ffi.NULL or stage.textInputTarget or lib.SDL_GetKeyboardFocus() != self.refs.window_[0]:
            return False
        for element in util.vec2list(stage.asUIElementContainer.children):
            if util.getClassName(element) in ('Controls', 'OptionsDialog', 'Graphics', 'Sound', 'GraphicsOptions', 'SoundsOptions', 'GameplayOptions', 'GameMenu'):
                return False
        loading = self.refs.manager._active.get('plugin_loading_probe')
        if loading:
            control = getattr(loading, 'control', None)
            if control and (control.state in ('holding', 'waiting', 'reconnecting', 'finishing', 'preview_pending') or control.pending):
                return False
            if getattr(loading, 'camera', None):
                return False
        return True

    def afterUpdate(self):
        held = self.held()
        pressed = held-self.old
        self.old = held  # Also consume edges during chat, loss of focus or load.
        if not pressed or not self.allowed(held):
            return
        wc, cw = self.refs.WorldClient, self.refs.ClientWorld
        if wc == ffi.NULL or cw == ffi.NULL or wc.overlay != ffi.NULL:
            return
        connection = wc.serverConnection
        if connection == ffi.NULL or connection.state != 3 or connection.socket < 0 or cw.player == ffi.NULL or cw.mySubWorld == ffi.NULL:
            return
        if util.getClassName(cw.player) not in ('PlayerCharacter', 'Wildfire', 'Duskwing', 'Ironclad', 'Fabricator', 'Ancient'):
            return
        player = ffi.cast('struct PlayerCharacter *', cw.player)
        props = ffi.cast('struct WorldObject *', player).props
        if props.hitpoints <= 0 or props.maxhitpoints <= 0:
            return
        for scan, slot in sorted(self.binds.items(), key=lambda pair: pair[1]):
            if scan in pressed and not any(code in self.old-pressed and value == slot for code, value in self.binds.items()):
                self.native.equip(wc, player, slot)
                break  # At most one inventory operation per update.
