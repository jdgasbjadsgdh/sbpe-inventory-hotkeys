"""Resolve exact native C++ methods from the user's Windows game, never launch it."""
import configparser
import hashlib
import io
import json
import os
import struct
import sys

NAMES = {
    'get_item': '_ZN15PlayerCharacter7getItemEi',
    'num_slots': '_ZN15PlayerCharacter9getNumPosE4Slot',
    'modify': '_ZN11WorldClient15handleModifyInvERK9ModifyInv',
    'construct': '_ZN9ModifyInvC1Ev',
    'destroy': '_ZN9ModifyInvD1Ev',
}
OPTIONAL_NAMES = {}
ALL_NAMES = NAMES



def image_relocations(data, sections):
    """Read PE32 HIGHLOW fixups, including operands near an entry boundary."""
    pe_at = struct.unpack_from('<I', data, 0x3c)[0]
    optional = pe_at+24
    if struct.unpack_from('<I', data, optional+92)[0] <= 5:
        return []
    rva, size = struct.unpack_from('<II', data, optional+96+5*8)
    if not size:
        return []
    def backed(address, count):
        for va, raw, length, _ in sections.values():
            if va <= address and address+count <= va+length:
                return raw+address-va
        raise ValueError('PE relocation outside a backed section')
    at = backed(rva, size);end = at+size;result = []
    while at < end:
        if end-at < 8:
            raise ValueError('Truncated PE relocation block')
        page, block = struct.unpack_from('<II', data, at)
        if block < 8 or block % 2 or at+block > end:
            raise ValueError('Invalid PE relocation block')
        for offset in range(at+8, at+block, 2):
            item = struct.unpack_from('<H', data, offset)[0]
            kind, address = item >> 12, page+(item & 4095)
            if kind == 0:
                continue
            if kind != 3:
                raise ValueError('Unsupported PE32 relocation type: '+str(kind))
            value = struct.unpack_from('<I', data, backed(address, 4))[0]
            result.append((address, value))
        at += block
    return result


def expected_entry(entry, preferred_base, loaded_base):
    """Compare relocated code, while keeping every other byte check intact."""
    expected = bytearray.fromhex(entry['entry_hex'])
    delta = loaded_base-preferred_base
    for fixup in entry.get('relocations', []):
        offset, original = fixup['offset'], fixup['value']
        if offset < -3 or offset >= len(expected):
            raise ValueError('Invalid entry relocation offset')
        before = struct.pack('<I', original)
        after = struct.pack('<I', (original+delta) & 0xffffffff)
        for i in range(4):
            if 0 <= offset+i < len(expected):
                if expected[offset+i] != before[i]:
                    raise ValueError('Entry relocation does not match file bytes')
                expected[offset+i] = after[i]
    return bytes(expected)


def pe(data):
    if data[:2] != b'MZ':
        raise ValueError('Not a PE executable')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset+4] != b'PE\0\0':
        raise ValueError('Missing PE signature')
    machine, count, _, symbols, number, size, _ = struct.unpack_from('<HHIIIHH', data, offset+4)
    if machine != 0x14c or struct.unpack_from('<H', data, offset+24)[0] != 0x10b:
        raise ValueError('Only the 32-bit x86 game is supported')
    base = struct.unpack_from('<I', data, offset+24+28)[0]
    sections = {}
    strings = symbols+number*18
    for n in range(count):
        at = offset+24+size+n*40
        name, virtualsize, va, rawsize, raw = struct.unpack_from('<8sIIII', data, at)
        name = name.rstrip(b'\0').decode('ascii')
        if name.startswith('/'):
            start = strings+int(name[1:]);name = data[start:data.index(b'\0', start)].decode('ascii')
        sections[name] = (va, raw, rawsize, virtualsize)
    return base, sections, symbols, number


def functions(data, base, sections, symbol_table, count):
    """Prefer exact COFF linkage names. DWARF is the fallback for stripped COFF."""
    found = {}
    strings = symbol_table+count*18
    i = 0
    if symbol_table:
        ordered = list(sections.values())
        while i < count:
            offset = symbol_table+i*18
            name, value, section, _, _, aux = struct.unpack_from('<8sIhHBB', data, offset)
            if name[:4] == b'\0'*4:
                at = strings+struct.unpack_from('<I', name, 4)[0]
                name = data[at:data.index(b'\0', at)]
            name = name.rstrip(b'\0').decode('ascii', errors='replace')
            for role, expected in ALL_NAMES.items():
                if name.lstrip('_') == expected.lstrip('_') and 0 < section <= len(ordered):
                    found[role] = ordered[section-1][0]+value
            i += aux+1
    if set(ALL_NAMES) <= set(found):
        return found
    # A partial COFF table can contain the loading methods but omit the
    # keyboard/mapping methods. Finish those from DWARF before returning.
    # Loading still works on builds with neither optional symbol.
    if '.debug_info' not in sections or '.debug_abbrev' not in sections:
        if set(NAMES) <= set(found):
            return found
        raise ValueError('Exact method symbols missing: '+', '.join(sorted(set(NAMES)-set(found))))
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'inventory_hotkeys_vendor'))
    from elftools.dwarf.dwarfinfo import DWARFInfo, DwarfConfig, DebugSectionDescriptor
    def descriptor(name):
        if name not in sections:
            return None
        va, raw, size, _ = sections[name]
        return DebugSectionDescriptor(io.BytesIO(data[raw:raw+size]), name, raw, size, base+va)
    dwarf = DWARFInfo(config=DwarfConfig(little_endian=True, default_address_size=4, machine_arch='x86'),
        debug_info_sec=descriptor('.debug_info'), debug_aranges_sec=descriptor('.debug_aranges'),
        debug_abbrev_sec=descriptor('.debug_abbrev'), debug_frame_sec=descriptor('.debug_frame'),
        eh_frame_sec=descriptor('.eh_frame'), debug_str_sec=descriptor('.debug_str'),
        debug_loc_sec=descriptor('.debug_loc'), debug_ranges_sec=descriptor('.debug_ranges'),
        debug_line_sec=descriptor('.debug_line'), debug_pubtypes_sec=descriptor('.debug_pubtypes'),
        debug_pubnames_sec=descriptor('.debug_pubnames'), debug_addr_sec=descriptor('.debug_addr'),
        debug_str_offsets_sec=descriptor('.debug_str_offsets'), debug_line_str_sec=descriptor('.debug_line_str'),
        debug_loclists_sec=descriptor('.debug_loclists'), debug_rnglists_sec=descriptor('.debug_rnglists'),
        debug_sup_sec=None, gnu_debugaltlink_sec=None)
    for cu in dwarf.iter_CUs():
        for die in cu.iter_DIEs():
            if die.tag != 'DW_TAG_subprogram':
                continue
            attrs = die.attributes
            name = attrs.get('DW_AT_linkage_name', attrs.get('DW_AT_MIPS_linkage_name'))
            low = attrs.get('DW_AT_low_pc')
            if not name or not low:
                continue
            name = name.value.decode('ascii', errors='replace')
            for role, expected in ALL_NAMES.items():
                if name.lstrip('_') == expected.lstrip('_'):
                    value = low.value
                    found.setdefault(role, value-base if value >= base else value)
    if not set(NAMES) <= set(found):
        raise ValueError('Exact method symbols missing: '+', '.join(sorted(set(NAMES)-set(found))))
    return found


def prepare(executable, root):
    with open(executable, 'rb') as stream:
        data = stream.read()
    base, sections, symbols, count = pe(data)
    resolved = functions(data, base, sections, symbols, count)
    relocations = image_relocations(data, sections)
    entries = {}
    for role, rva in resolved.items():
        candidates = [(raw+rva-va, raw+size) for va, raw, size, _ in sections.values() if va <= rva < va+size]
        if len(candidates) != 1 or candidates[0][0]+24 > candidates[0][1]:
            raise ValueError('Method outside a backed executable section')
        raw, _ = candidates[0]
        entries[role] = {'rva': rva, 'entry_hex': data[raw:raw+24].hex(), 'linkage_name': ALL_NAMES[role],
                         'relocations': [{'offset': address-rva, 'value': value} for address, value in relocations if address < rva+24 and address+4 > rva]}
    output = {'schema': 1, 'architecture': 'x86', 'game_sha256': hashlib.sha256(data).hexdigest(),
              'image_base': base, 'functions': entries, 'calling_convention': 'Microsoft x86 thiscall', 'live_validated': False}
    path = os.path.join(root, 'inventory-hotkeys-target.json')
    with open(path+'.new', 'w') as stream:
        json.dump(output, stream, indent=2)
    os.replace(path+'.new', path)
    return output


if __name__ == '__main__':
    root = os.path.dirname(os.path.abspath(__file__))
    cfg = configparser.ConfigParser();cfg.read(os.path.join(root, 'config.ini'))
    executable = sys.argv[1] if len(sys.argv)>1 else cfg.get('general', 'game')
    prepare(executable, root)
    print('Inventory hotkey method addresses prepared for this exact executable. No game launched.')
