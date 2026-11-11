import sys; sys.path.insert(0,'/home/claude/anim/field')
from navgrid import Grid
import __main__; __main__.Grid = Grid
import drawmap as D
b=[float(v) for v in sys.argv[1].split(',')]
D.render(tuple(b[:4]), b[4], sys.argv[2], title=sys.argv[3] if len(sys.argv)>3 else '')
