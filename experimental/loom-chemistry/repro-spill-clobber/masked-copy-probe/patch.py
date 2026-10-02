"""Diagnostic only: replace one loop-copy sequence with a trampoline.

Requires the exact rotate-32-3.hsaco emitted in the spill-free experiment.
Does not change register allocation, kernel metadata, or the loop condition.
"""
import pathlib
import struct
import subprocess
import sys

source = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
original = source.read_bytes()
assert original[:6] == b'\x7fELF\x02\x01'
assert struct.unpack_from('<5I', original, 0x1084) == (
    0x7E100306, 0x7E0C0307, 0x7E0E0302, 0x7E040308, 0xBF82FFF5)

def branch(pc, target):
    delta = (target - pc - 4) // 4
    assert -32768 <= delta <= 32767
    return struct.pack('<I', 0xBF820000 | (delta & 0xffff))

for name, mask in [('masked', 's_nop 0'),
                   ('all', 's_mov_b64 exec, -1'),
                   ('lane1', 's_or_b64 exec, exec, 2')]:
    asm = out / (name + '.s')
    asm.write_text('.text\ns_mov_b64 s[8:9], exec\n' + mask + '''
v_mov_b32_e32 v8, v6
v_mov_b32_e32 v6, v7
v_mov_b32_e32 v7, v2
v_mov_b32_e32 v2, v8
s_mov_b64 exec, s[8:9]
''')
    obj = asm.with_suffix('.o')
    raw = asm.with_suffix('.bin')
    subprocess.run(['/opt/rocm/llvm/bin/llvm-mc', '-triple=amdgcn-amd-amdhsa',
                    '-mcpu=gfx942', '-filetype=obj', str(asm), '-o', str(obj)], check=True)
    subprocess.run(['/opt/rocm/llvm/bin/llvm-objcopy', '-O', 'binary',
                    '--only-section=.text', str(obj), str(raw)], check=True)
    code = raw.read_bytes()
    start = 0x1170
    code += branch(start + len(code), 0x106c)
    end = start + len(code)
    data = bytearray(original)
    assert end < 0x2000
    data[start:end] = code
    data[0x1084:0x1088] = branch(0x1084, start)
    # Extend the existing executable segment and .text into its file padding.
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', data, 54)
    hits = 0
    for i in range(phnum):
        pos = phoff + i * phsize
        typ, flags, offset = struct.unpack_from('<IIQ', data, pos)
        if typ == 1 and flags & 1:
            assert offset == 0x1000
            struct.pack_into('<QQ', data, pos + 32, end - offset, end - offset)
            hits += 1
    assert hits == 1
    shoff = struct.unpack_from('<Q', data, 40)[0]
    shsize, shnum = struct.unpack_from('<HH', data, 58)
    hits = 0
    for i in range(shnum):
        pos = shoff + i * shsize
        flags, address, offset = struct.unpack_from('<QQQ', data, pos + 8)
        if flags & 4:
            assert address == offset == 0x1000
            struct.pack_into('<Q', data, pos + 32, end - offset)
            hits += 1
    assert hits == 1
    (out / (name + '.hsaco')).write_bytes(data)
