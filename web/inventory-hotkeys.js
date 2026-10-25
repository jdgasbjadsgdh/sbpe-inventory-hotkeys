/* SBPE INVENTORY HOTKEYS — WebBreak build 1574197275 */
(function (root, factory) {
  'use strict';
  var api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else if (root.WebBreak && root.WebBreak.sbpe) api.install(root.WebBreak, root);
  else if (Array.isArray(root.WebBreakPlugins)) root.WebBreakPlugins.push(function () { api.install(root.WebBreak, root); });
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  // EDIT KEYS HERE. Browser KeyboardEvent.code names: "KeyZ", "Digit1", "F2".
  // "" leaves a slot unbound. Slot 1 is top-left; count across, then down.
  // Example: 1: "KeyZ", 2: "KeyX", 3: "Digit1".
  // Edit this block in your installed Overrides file, save, then reload.
  var SLOT_KEYS = {
    1: "", 2: "", 3: "",
    4: "", 5: "", 6: "",
    7: "", 8: "", 9: "",
    10: "", 11: "", 12: ""
  };
  // END INVENTORY KEY CONFIG
  function install(WB, host) {
    if (WB.build !== '1574197275') throw new Error('Inventory hotkeys require WebBreak 1574197275.');
    var sbpe = WB.sbpe, mem = WB.mem, doc = host.document;
    class Plugin extends sbpe.PluginBase {
      onInit() {
        var self = this; this.down = new Set(); this.owned = new Set(); this.pending = null; this.binds = new Map();
        Object.keys(SLOT_KEYS).forEach(function (slot) {
          var code = SLOT_KEYS[slot];
          if (!code) return;
          if (!/^(Key[A-Z]|Digit[0-9]|F([1-9]|1[0-2])|Space|Tab|Backquote|Minus|Equal|BracketLeft|BracketRight|Semicolon|Quote|Backslash|Comma|Period|Slash|Arrow(Up|Down|Left|Right))$/.test(code) || self.binds.has(code))
            throw new Error('Invalid or duplicate inventory hotkey: '+code);
          self.binds.set(code, Number(slot)-1);
        });
        this.keydown = function (e) {
          var was = self.down.has(e.code); self.down.add(e.code);
          if (self.owned.has(e.code)) { e.preventDefault(); e.stopImmediatePropagation(); return; }
          if (was || e.repeat || !self.binds.has(e.code) || !self.allowed(e)) return;
          var c = self.context(); if (!c) return;
          self.pending = { slot: self.binds.get(e.code), token: c.token };
          self.owned.add(e.code); e.preventDefault(); e.stopImmediatePropagation();
        };
        this.keyup = function (e) {
          self.down.delete(e.code);
          if (self.owned.delete(e.code)) { e.preventDefault(); e.stopImmediatePropagation(); }
        };
        this.blur = function () { self.down.clear(); self.owned.clear(); self.pending = null; };
        host.addEventListener('keydown', this.keydown, true); host.addEventListener('keyup', this.keyup, true); host.addEventListener('blur', this.blur);
        WB.inventoryHotkeys = { plugin: this, keys: Object.assign({}, SLOT_KEYS) };
      }
      allowed(e) {
        if (!doc || !doc.hasFocus() || (e && (e.ctrlKey || e.altKey || e.metaKey))) return false;
        if (['ControlLeft','ControlRight','AltLeft','AltRight','MetaLeft','MetaRight'].some((key)=>this.down.has(key))) return false;
        var active = doc.activeElement;
        if ((active && (active.isContentEditable || /^(INPUT|TEXTAREA|SELECT|BUTTON)$/.test(active.tagName))) || sbpe.sdl.IsTextInputActive()) return false;
        var st = this.refs.stage;
        if (!st || st.textInputTarget || this.refs.topTypes.some(function (x) { return /^(Controls|OptionsDialog|Graphics|Sound|GraphicsOptions|SoundsOptions|GameplayOptions|GameMenu)$/.test(x); })) return false;
        var loading = this.refs.manager && this.refs.manager._active.plugin_sc_boss_test;
        return !(loading && (['holding','waiting','reconnecting','finishing','preview_pending'].indexOf(loading.mode)>=0 || loading.pending || loading.finishPending || loading.camera));
      }
      context() {
        var wc = this.refs.WorldClient, cw = this.refs.ClientWorld;
        if (!wc || !cw || wc.overlay || !cw.player || !cw.mySubWorld) return null;
        var connection = wc.serverConnection;
        if (!connection || connection.state !== 3 || connection.socket < 0) return null;
        var player = cw.player;
        if (['PlayerCharacter','Wildfire','Duskwing','Ironclad','Fabricator','Ancient'].indexOf(player.className())<0) return null;
        player = player.cast('PlayerCharacter');
        if (player.props.hitpoints<=0 || player.props.maxhitpoints<=0) return null;
        return { wc:wc, player:player, token:[wc.address,cw.address,player.address,connection.address,connection.socket,cw.id].join(':') };
      }
      afterUpdate() {
        var queued = this.pending; this.pending = null;
        if (!queued || !this.allowed()) return;
        var c = this.context(); if (!c || c.token !== queued.token) return;
        var asm = WB.asm;
        if (!asm || typeof asm.inventory_hotkeys_modify !== 'function') throw new Error('Install the inventory export adapter before using its keys.');
        var from = 0xf00+queued.slot;
        if (queued.slot<0 || queued.slot>=12 || queued.slot>=asm.inventory_hotkeys_slots(c.player.address,0xf00)) return;
        var item = mem.cast('Item',asm.inventory_hotkeys_item(c.player.address,from));
        if (!item || !item.user || item.user.address!==c.player.address || item.slotPos!==from || !item.desc) return;
        var target = item.desc.slot;
        if ([0x100,0x200,0x300,0x400,0x500,0x600].indexOf(target)<0 || asm.inventory_hotkeys_slots(c.player.address,target)<=0) return;
        var p = asm._malloc(24); if (!p) return;
        try {
          asm.inventory_hotkeys_construct(p);
          var request = mem.cast('ModifyInv',p); request.command=2; request.slotpos=from; request.targetslotpos=target; mem.setI32(request.addressOf('_has_bits'),7);
          asm.inventory_hotkeys_modify(c.wc.address,p);
        } finally { asm.inventory_hotkeys_destroy(p); asm._free(p); }
      }
      onUnload() {
        host.removeEventListener('keydown',this.keydown,true);host.removeEventListener('keyup',this.keyup,true);host.removeEventListener('blur',this.blur);
        this.blur(); if (WB.inventoryHotkeys && WB.inventoryHotkeys.plugin===this) delete WB.inventoryHotkeys;
      }
    }
    sbpe.register('inventory_hotkeys', Plugin); return Plugin;
  }
  return {install:install};
});
/* END SBPE INVENTORY HOTKEYS */
