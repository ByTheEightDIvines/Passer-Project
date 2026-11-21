import sys,zipfile,struct; sys.path[:0]=['/home/claude/w/anim']
from rarc import rarc,yaz0; from rarcwrite import write_rarc
z=zipfile.ZipFile('/mnt/user-data/uploads/Dusklight/mods/Dynamic Travelers.dusk')
d=z.read('overlay/res/Object/P_Beedl.arc'); raw=yaz0(d) if d[:4]==b'Yaz0' else d
node0=0x20+struct.unpack_from('>I',raw,0x24)[0]; stro=0x20+struct.unpack_from('>I',raw,0x34)[0]
root=raw[stro+struct.unpack_from('>I',raw,node0+4)[0]:].split(b'\0')[0].decode()
files=dict(rarc(raw)); print('root',root,len(files))
assert files['bmdr/beedle.bmd']==open('/home/claude/beedle/beedle.bmd','rb').read()
assert files['bmdr/beedle_tent.bmd']==open('/home/claude/beedle/beedle_tent.bmd','rb').read()
for n in ['beedle','beedle_tent']:
    files['bmdr/%s.bmd'%n]=open('/home/claude/beedle/new_%s.bmd'%n,'rb').read()
    files['btp/%s.btp'%n]=open('/home/claude/beedle/new_%s.btp'%n,'rb').read()
write_rarc('/home/claude/pkg/P_Beedl.arc',root,files)
chk=rarc(open('/home/claude/pkg/P_Beedl.arc','rb').read())
assert len(chk)==len(files) and all(chk[k]==v for k,v in files.items()); print('ok',[k for k in chk if not k.startswith('bck')])
