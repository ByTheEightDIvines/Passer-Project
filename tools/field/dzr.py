"""Room/stage actor list reader (dzr/dzs): ACTR/SCOB/TGOB/TRES/SCLS chunks."""
import struct


def chunks(d):
    n = struct.unpack_from('>I', d, 0)[0]
    out = []
    for i in range(n):
        tag, cnt, off = struct.unpack_from('>4sII', d, 4 + i * 12)
        out.append((tag.decode('latin1'), cnt, off))
    return out


def actors(d):
    res = []
    for tag, cnt, off in chunks(d):
        base = tag[:3] if tag[3:] in '0123456789ab' else tag
        if base in ('ACT', 'TGO', 'TRE') or tag in ('ACTR', 'TGOB', 'TRES'):
            size = 0x20
        elif base in ('SCO', 'TGS') or tag in ('SCOB', 'TGSC'):
            size = 0x24
        else:
            continue
        for k in range(cnt):
            e = off + k * size
            name = d[e:e + 8].split(b'\0')[0].decode('latin1')
            prm, x, y, z = struct.unpack_from('>Ifff', d, e + 8)
            ax, ay, az, en = struct.unpack_from('>hhhH', d, e + 0x18)
            res.append(dict(layer_tag=tag, name=name, params=prm, pos=(x, y, z), angle=(ax, ay, az), enemy_no=en))
    return res


def scls(d):
    out = []
    for tag, cnt, off in chunks(d):
        if tag != 'SCLS':
            continue
        for k in range(cnt):
            e = off + k * 0xD
            stage = d[e:e + 8].split(b'\0')[0].decode('latin1')
            start, room, wipe = struct.unpack_from('>bbb', d, e + 8)
            out.append(dict(index=k, stage=stage, start=start, room=room))
    return out
