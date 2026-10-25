"""Standalone Windows port, without a live Windows game or external service."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent/'windows'
sys.path.insert(0, str(ROOT))
from inventory_hotkeys_native import NativeInventory
from inventory_hotkeys_resolver import NAMES, pe, functions, prepare, expected_entry
from inventory_hotkeys_x86 import call_thunk


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


class PortTests(unittest.TestCase):
    def test_normal_request_and_cleanup(self):
        import cffi
        # Model the Windows 32-bit vtable word even on this 64-bit test host.
        ffi=cffi.FFI();ffi.cdef('struct ModifyInv { unsigned int v; unsigned int command; int slotpos,targetslotpos,cached; unsigned int _has_bits[1]; };')
        native=NativeInventory.__new__(NativeInventory);native.ffi=ffi
        calls=[];owner=object();item=types.SimpleNamespace(user=owner,slotPos=0xf00,desc=types.SimpleNamespace(slot=0x100))
        native.num_slots=lambda p,slot:12 if slot==0xf00 else 1
        native.get_item=lambda p,slot:item if slot==0xf00 else ffi.NULL
        native.construct=lambda p:calls.append('ctor')
        native.destroy=lambda p:calls.append('dtor')
        native.modify=lambda wc,p:calls.append((p.command,p.slotpos,p.targetslotpos,p._has_bits[0]))
        self.assertEqual(ffi.sizeof('struct ModifyInv'),24)
        self.assertTrue(native.equip(object(),owner,0));self.assertEqual(calls,['ctor',(2,0xf00,0x100,7),'dtor'])
        calls.clear();self.assertFalse(native.equip(object(),owner,1));self.assertEqual(calls,[])
        item.desc.slot=0xf00;self.assertFalse(native.equip(object(),owner,0));self.assertEqual(calls,[])
        item.desc.slot=0x600;self.assertTrue(native.equip(object(),owner,0));self.assertEqual(calls[1][2],0x600)
        def error(wc,p):raise ValueError('native request rejected')
        native.modify=error;calls.clear()
        with self.assertRaises(ValueError):native.equip(object(),owner,0)
        self.assertEqual(calls,['ctor','dtor'])

    def test_keys_and_context(self):
        class FFI:
            NULL=None
            def new(self,t):return [512]
            def cast(self,t,p):return p
        class Base:
            def __init__(self,refs):self.refs=refs
        keys=[0]*512;focus=[1];typing=[False]
        lib=types.SimpleNamespace(SDL_GetKeyboardState=lambda n:keys,SDL_GetScancodeFromName=lambda s:{b'Z':29,b'X':27}.get(s,0),SDL_IsTextInputActive=lambda:typing[0],SDL_GetKeyboardFocus=lambda:focus[0])
        mods={'_remote':types.SimpleNamespace(ffi=FFI(),lib=lib),'manager':types.SimpleNamespace(PluginBase=Base),'util':types.SimpleNamespace(getClassName=lambda p:p.kind,vec2list=lambda v:v)}
        with patch.dict(sys.modules,mods):
            pmod=module(ROOT/'plugins/inventory_hotkeys.py','inventory_plugin_fixture')
        calls=[]
        class Native:
            def __init__(self,*args):pass
            def equip(self,wc,pc,slot):calls.append(slot)
        pmod.NativeInventory=Native;pmod.SLOT_KEYS={1:'Z',2:'X'}
        stage=types.SimpleNamespace(textInputTarget=None,asUIElementContainer=types.SimpleNamespace(children=[]))
        pc=types.SimpleNamespace(kind='Wildfire',props=types.SimpleNamespace(hitpoints=100,maxhitpoints=100))
        conn=types.SimpleNamespace(state=3,socket=4)
        wc=types.SimpleNamespace(overlay=None,serverConnection=conn);cw=types.SimpleNamespace(player=pc,mySubWorld=1)
        refs=types.SimpleNamespace(stage=[stage],window_=[1],WorldClient=wc,ClientWorld=cw,manager=types.SimpleNamespace(_active={}))
        p=pmod.Plugin(refs);p.onInit()
        keys[29]=1;p.afterUpdate();p.afterUpdate();self.assertEqual(calls,[0]);keys[29]=0;p.afterUpdate()
        typing[0]=True;keys[29]=1;p.afterUpdate();typing[0]=False;p.afterUpdate();self.assertEqual(calls,[0]);keys[29]=0;p.afterUpdate()
        focus[0]=2;keys[27]=1;p.afterUpdate();focus[0]=1;p.afterUpdate();self.assertEqual(calls,[0]);keys[27]=0;p.afterUpdate()
        refs.manager._active['plugin_loading_probe']=types.SimpleNamespace(control=types.SimpleNamespace(state='holding',pending=None),camera=None)
        keys[27]=1;p.afterUpdate();self.assertEqual(calls,[0]);keys[27]=0;p.afterUpdate();refs.manager._active.clear()
        keys[27]=1;p.afterUpdate();self.assertEqual(calls,[0,1]);keys[27]=0;p.afterUpdate()
        conn.state=4;keys[29]=1;p.afterUpdate();self.assertEqual(calls,[0,1])

    def test_installer_preserves_existing_keys(self):
        installer=module(ROOT/'install.py','inventory_install_fixture')
        with tempfile.TemporaryDirectory() as tmp:
            dest=Path(tmp);(dest/'manager.py').write_text('');(dest/'plugins').mkdir();(dest/'pypy').mkdir()
            installer.install(dest)
            plugin=dest/'plugins/inventory_hotkeys.py';plugin.write_text(plugin.read_text().replace('1: "", 2: "", 3: "",','1: "Z", 2: "X", 3: "",'))
            config=dest/'config.ini';config.write_text('[existing]\nkey = keep\n')
            installer.install(dest);text=plugin.read_text();self.assertIn("1: 'Z'",text);self.assertIn("2: 'X'",text);self.assertEqual(config.read_text(),'[existing]\nkey = keep\n')
            self.assertTrue(list((dest/'inventory-hotkeys-backups').rglob('inventory_hotkeys.py')))

    def test_exact_coff_resolver(self):
        data=bytearray(4096);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,0x80);data[0x80:0x84]=b'PE\0\0'
        sym=0x900;struct.pack_into('<HHIIIHH',data,0x84,0x14c,1,0,sym,len(NAMES),0xe0,0)
        struct.pack_into('<H',data,0x98,0x10b);struct.pack_into('<I',data,0x98+28,0x400000)
        struct.pack_into('<8sIIII',data,0x178,b'.text',0x400,0x1000,0x400,0x400)
        names=bytearray(b'\0'*4)
        for i,(role,name) in enumerate(NAMES.items()):
            at=len(names);names.extend(name.encode()+b'\0');struct.pack_into('<IIIhHBB',data,sym+i*18,0,at,i*32,1,0x20,2,0)
            data[0x400+i*32:0x400+i*32+24]=bytes([i+1])*24
        struct.pack_into('<I',names,0,len(names));at=sym+len(NAMES)*18;data[at:at+len(names)]=names
        base,sections,symbols,count=pe(data);found=functions(data,base,sections,symbols,count);self.assertEqual(set(found),set(NAMES))
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'fixture.exe';exe.write_bytes(data);target=prepare(str(exe),tmp)
            for i,(role,entry) in enumerate(target['functions'].items()):self.assertEqual(expected_entry(entry,base,base),bytes([i+1])*24)

    def test_real_x86_thiscall_adapter(self):
        from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_EAX
        for argument in (False,True):
            u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x50000)
            u.mem_write(0x11000,call_thunk(0x12000,argument))
            u.mem_write(0x12000,b'\x89\xc8'+(b'\x03\x44\x24\x04\xc2\x04\x00' if argument else b'\xc3'))
            u.mem_write(0x50000,struct.pack('<III',0x13000,123,45));u.reg_write(UC_X86_REG_ESP,0x50000);u.reg_write(UC_X86_REG_EBP,0x44444)
            u.emu_start(0x11000,0x13000,count=100)
            self.assertEqual(u.reg_read(UC_X86_REG_EAX),168 if argument else 123);self.assertEqual(u.reg_read(UC_X86_REG_EBP),0x44444);self.assertEqual(u.reg_read(UC_X86_REG_ESP),0x50004)


if __name__=='__main__':unittest.main()
