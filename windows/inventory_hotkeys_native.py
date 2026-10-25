"""Normal inventory calls for Windows x86 SBPE; no game-method patches."""
import ctypes
import hashlib
import json
import os
import platform
from inventory_hotkeys_resolver import prepare, expected_entry
from inventory_hotkeys_x86 import call_thunk


class NativeInventory:
    def __init__(self, refs, ffi):
        if platform.system() != 'Windows' or ffi.sizeof('void *') != 4:
            raise RuntimeError('Requires Windows SBPE 1.7.0 with its bundled 32-bit PyPy')
        if ffi.sizeof('struct ModifyInv') != 24 or ffi.offsetof('struct ModifyInv', '_has_bits') != 20:
            raise RuntimeError('Inventory request layout differs from supported Windows ABI')
        self.ffi = ffi
        self.memory = []
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.GetModuleHandleW.restype = ctypes.c_void_p
        self.kernel.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
        self.kernel.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_ulong]
        self.kernel.VirtualAlloc.restype = ctypes.c_void_p
        self.kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_ulong, ctypes.c_ulong]
        self.kernel.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong)]
        self.kernel.FlushInstructionCache.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        filename = ctypes.create_unicode_buffer(32768)
        if not self.kernel.GetModuleFileNameW(None, filename, len(filename)):
            raise ctypes.WinError(ctypes.get_last_error())
        base = self.kernel.GetModuleHandleW(None)
        with open(filename.value, 'rb') as stream:
            digest = hashlib.sha256(stream.read()).hexdigest()
        path = os.path.join(refs.SCRIPTPATH, 'inventory-hotkeys-target.json')
        try:
            with open(path) as stream:
                target = json.load(stream)
        except (IOError, ValueError):
            target = {}
        if target.get('game_sha256') != digest:
            target = prepare(filename.value, refs.SCRIPTPATH)
        if target.get('architecture') != 'x86' or target.get('schema') != 1:
            raise RuntimeError('Unsupported inventory method manifest')
        signatures = {
            'get_item': ('struct Item *(*)(void *,int)', True),
            'num_slots': ('int(*)(void *,int)', True),
            'modify': ('void(*)(void *,void *)', True),
            'construct': ('void(*)(void *)', False),
            'destroy': ('void(*)(void *)', False),
        }
        for name, (signature, argument) in signatures.items():
            entry = target['functions'][name]
            address = base+entry['rva']
            expected = expected_entry(entry, target['image_base'], base)
            if len(expected) != 24 or ctypes.string_at(address, 24) != expected:
                raise RuntimeError('Inventory native method differs or is hooked: '+name)
            setattr(self, name, ffi.cast(signature, self.allocate(call_thunk(address, argument))))
        # Adapters contain executable code only, no callbacks into Python.
        # Keep them alive across SBPE's plugin reloads.
        refs.setdefault('_inventory_hotkeys_keepalive', []).append(self)

    def allocate(self, code):
        address = self.kernel.VirtualAlloc(None, len(code), 0x3000, 0x04)
        if not address:
            raise ctypes.WinError(ctypes.get_last_error())
        ctypes.memmove(address, code, len(code))
        old = ctypes.c_ulong()
        if not self.kernel.VirtualProtect(address, len(code), 0x20, ctypes.byref(old)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not self.kernel.FlushInstructionCache(ctypes.c_void_p(-1), address, len(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        self.memory.append(address)
        return address

    def equip(self, wc, player, slot):
        if not 0 <= slot < 12 or slot >= self.num_slots(player, 0xf00):
            return False
        item = self.get_item(player, 0xf00+slot)
        if item == self.ffi.NULL or item.user != player or item.slotPos != 0xf00+slot or item.desc == self.ffi.NULL:
            return False
        target = int(item.desc.slot)
        if target not in (0x100, 0x200, 0x300, 0x400, 0x500, 0x600) or self.num_slots(player, target) <= 0:
            return False
        request = self.ffi.new('struct ModifyInv *')
        self.construct(request)
        try:
            request.command = 2
            request.slotpos = 0xf00+slot
            request.targetslotpos = target
            request._has_bits[0] = 7
            # This is the ordinary UI handler: it checks compatibility and the
            # input budget, swaps locally and sends only accepted changes.
            self.modify(wc, request)
        finally:
            self.destroy(request)
        return True
