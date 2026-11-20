"""Renumber the effects in our own psr_fx.jpc to 0xF000 + their index (the range no game pack uses).

    python3 -I jpcreid.py PsrPack.arc out/PsrPack.arc

Only psr_fx.jpc inside the arc changes (each resource header's u16 id); every other file is checked to come
out byte-identical. The code's names for these numbers are the kFx* constants in src/freeze.hpp; keep
the order there in step with the pack's order (printed here).
"""
import struct
import sys

import jpc
import rarc
import rarc_replace

src, dst = sys.argv[1], sys.argv[2]
arc = open(src, 'rb').read()
files = rarc.rarc(arc)
data = bytearray(files['psr_fx.jpc'])
res, _, _ = jpc.parse(bytes(data))
for i, r in enumerate(res):
    print('0x%04x -> 0x%04x' % (r['id'], 0xF000 + i))
    struct.pack_into('>H', data, r['start'], 0xF000 + i)
out = rarc_replace.replace(arc, 'psr_fx.jpc', bytes(data))
check = rarc.rarc(out)
for name, body in files.items():
    if name != 'psr_fx.jpc':
        assert check[name] == body, name
assert check['psr_fx.jpc'] == bytes(data)
open(dst, 'wb').write(out)
