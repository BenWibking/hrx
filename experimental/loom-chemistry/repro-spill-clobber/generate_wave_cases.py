from pathlib import Path
import sys
from generate_live_history import generate
kind=sys.argv[1];n=int(sys.argv[2]);bits=int(sys.argv[3]);s=generate(n,kind,8)
s=s.replace('%four = index.constant 4 : index','%four = index.constant 64 : index')
s=s.replace(f'<{4*n}xi32>',f'<{64*n}xi32>').replace(f'<{4*n*8}xi32>',f'<{64*n*8}xi32>')
s=s.replace('%eff = scalar.muli %li, %flag : i32','%three = scalar.constant 3 : i32\n %odd = scalar.constant 3 : i32\n %scrambled = scalar.muli %li,%odd : i32\n %class = scalar.andi %scrambled,%three : i32\n %eff = scalar.muli %class,%flag : i32')
for h in range(8):s=s.replace(f'%outbase{h} = index.constant {h*4*n} : index',f'%outbase{h} = index.constant {h*64*n} : index')
if bits==64:s=s.replace('i32','i64')
Path(sys.argv[4]).write_text(s)
