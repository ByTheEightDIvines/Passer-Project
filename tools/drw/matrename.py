"""Rename materials in a BMD's MAT3 name table (same length only) and update their JUTNameTab hashes.
Used to keep other mods that pick materials by name (Live Reflections: al_kt..., al_SH...) off our copies."""
import struct, sys
sys.path.insert(0, '/home/claude/drw')
from drwscan import secs
def key(s):
    h = 0
    for c in s.encode('latin1'): h = (h * 3 + c) & 0xFFFF
    return h
def rename(d, fn):
    d = bytearray(d); S = secs(bytes(d)); off, m = S[b'MAT3']
    no = struct.unpack('>I', m[0x14:0x18])[0]; cnt = struct.unpack('>H', m[no:no + 2])[0]; done = []
    for i in range(cnt):
        e = no + 4 + i * 4; h, so = struct.unpack('>HH', m[e:e + 4])
        a = no + so; b = m.index(b'\0', a); old = m[a:b].decode('latin1'); new = fn(old)
        if new == old: continue
        assert len(new) == len(old), (old, new)
        d[off + a: off + b] = new.encode('latin1'); struct.pack_into('>H', d, off + e, key(new)); done.append((old, new))
    return bytes(d), done
