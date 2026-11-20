import struct, sys
sys.path.insert(0, '/home/claude/scripts')
import jpc
def align(n, a=0x20): return (n + a - 1) & ~(a - 1)
def merge(base, donor, ids):
    br, bt, bto = jpc.parse(base); dr, dt, _ = jpc.parse(donor)
    texdata = [base[o:o+s] for n, o, s in bt]
    names = [n for n, o, s in bt]
    out_res = [base[r['start']:r['end']] for r in br]
    for want in ids:
        r = [x for x in dr if x['id'] == want][0]
        blob = bytearray(donor[r['start']:r['end']])
        tdbn = blob[6]
        for mg, o, sz in r['blocks']:
            if mg != 'TDB1': continue
            lo = o - r['start']
            for k in range(tdbn):
                old = struct.unpack('>H', blob[lo+8+2*k:lo+10+2*k])[0]
                name, to, ts = dt[old]
                data = donor[to:to+ts]
                if data in texdata: new = texdata.index(data)
                else:
                    texdata.append(data); names.append(name); new = len(texdata) - 1
                struct.pack_into('>H', blob, lo+8+2*k, new)
        out_res.append(bytes(blob))
    body = b''.join(out_res)
    texoff = align(0x10 + len(body))
    out = bytearray(b'JPAC2-10' + struct.pack('>HHI', len(out_res), len(texdata), texoff))
    out += body
    out += b'\0' * (texoff - len(out))
    for t in texdata: out += t
    return bytes(out), names
if __name__ == '__main__':
    base = open(sys.argv[1], 'rb').read(); donor = open(sys.argv[2], 'rb').read()
    ids = [int(x, 16) for x in sys.argv[4].split(',')]
    out, names = merge(base, donor, ids)
    open(sys.argv[3], 'wb').write(out)
    r, t, _ = jpc.parse(out)
    print(len(out), [hex(x['id']) for x in r], names)
