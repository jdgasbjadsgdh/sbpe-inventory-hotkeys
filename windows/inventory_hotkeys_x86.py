"""32-bit Microsoft thiscall/cdecl adapters; no executable addresses embedded."""
import struct


def pointer(value):
    if not 0 < value <= 0xffffffff:
        raise ValueError('Expected a non-null 32-bit address')
    return struct.pack('<I', value)


def callback_thunk(callback, argument=False):
    # ECX=this, [entry ESP+4]=argument. Align stack for the Python/libffi callback.
    code = b'\x55\x89\xe5\x83\xe4\xf0\x83\xec\x10\x89\x0c\x24'
    if argument:
        code += b'\x8b\x45\x08\x89\x44\x24\x04'
    code += b'\xb8' + pointer(callback) + b'\xff\xd0\x89\xec\x5d'
    return code + (b'\xc2\x04\x00' if argument else b'\xc3')


def call_thunk(target, argument=False):
    # cdecl(this, argument) -> thiscall target; restore ESP independently of
    # the native callee's RET 4, preserving nonvolatile EBP and return EAX.
    code = b'\x55\x89\xe5\x83\xe4\xf0\x83\xec\x10\x8b\x4d\x08'
    if argument:
        code += b'\x8b\x45\x0c\x89\x04\x24'
    return code + b'\xb8' + pointer(target) + b'\xff\xd0\x89\xec\x5d\xc3'


def call_two_thunk(target):
    # cdecl(this, down, key) -> Stage::handleKeyboardEvent(thiscall; RET 8).
    return (b'\x55\x89\xe5\x83\xe4\xf0\x83\xec\x10\x8b\x4d\x08'
            b'\x8b\x45\x0c\x89\x04\x24\x8b\x45\x10\x89\x44\x24\x04'
            b'\xb8'+pointer(target)+b'\xff\xd0\x89\xec\x5d\xc3')
