import sys,zipfile,struct; sys.path[:0]=['/home/claude/w/anim']
from rarc import rarc,yaz0; from rarcwrite import write_rarc
z=zipfile.ZipFile('/home/claude/pkg/Dynamic Travelers.dusk')
# the cook's own archive
write_rarc('/home/claude/pkg/P_Cook.arc','P_Cook',{'bmdr/psr_cook.bmd':open('/home/claude/cook/psr_cook.bmd','rb').read(),
                                                   'btp/psr_cook.btp':open('/home/claude/cook/psr_cook.btp','rb').read()})
c=rarc(open('/home/claude/pkg/P_Cook.arc','rb').read()); print('P_Cook',sorted(c))
# the spoon, in PsrPack
d=z.read('overlay/res/Object/PsrPack.arc'); raw=yaz0(d) if d[:4]==b'Yaz0' else d
node0=0x20+struct.unpack_from('>I',raw,0x24)[0]; stro=0x20+struct.unpack_from('>I',raw,0x34)[0]
root=raw[stro+struct.unpack_from('>I',raw,node0+4)[0]:].split(b'\0')[0].decode()
files=dict(rarc(raw)); assert 'bmdr/psr_spoon.bmd' in files
files['bmdr/psr_spoon.bmd']=open('/home/claude/cook/psr_spoon.bmd','rb').read()
write_rarc('/home/claude/pkg/PsrPack.arc',root,files)
chk=rarc(open('/home/claude/pkg/PsrPack.arc','rb').read())
assert len(chk)==len(files) and all(chk[k]==v for k,v in files.items()); print('PsrPack',root,len(chk),'ok')
