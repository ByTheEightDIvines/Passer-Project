import sys,zipfile,struct; sys.path[:0]=['/home/claude/w/anim']
from rarc import rarc,yaz0; from rarcwrite import write_rarc
z=zipfile.ZipFile('/mnt/user-data/uploads/Dusklight/mods/Dynamic Travelers.dusk')
d=z.read('overlay/res/Object/PsrTent.arc'); raw=yaz0(d) if d[:4]==b'Yaz0' else d
# root node's name, from the RARC header
ho=0x20; node0=ho+struct.unpack_from('>I',raw,0x20+0x04)[0]; stro=ho+struct.unpack_from('>I',raw,0x20+0x14)[0]
nameoff=struct.unpack_from('>I',raw,node0+4)[0]; root=raw[stro+nameoff:raw.index(b'\0',stro+nameoff)].decode()
files=dict(rarc(raw)); print('root',root,len(files))
files['bmdr/psr_bedroll.bmd']=open('/home/claude/bedroll/psr_bedroll.bmd','rb').read()
write_rarc('/home/claude/pkg/PsrTent.arc',root,files)
chk=rarc(open('/home/claude/pkg/PsrTent.arc','rb').read())
assert all(chk[k]==v for k,v in files.items()) and len(chk)==len(files); print('ok',sorted(chk))
