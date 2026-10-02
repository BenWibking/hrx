from pathlib import Path
import re,sys
from generate_high_pressure import generate
n=int(sys.argv[1]);s=generate(n);start=s.index('  %iterations,');end=s.index('  %diff0',start);types=', '.join(['i32']*n);body='';old='v'
for stage in range(4):
 body+=f'  %stage{stage} = scalar.constant {stage} : i32\n  %active{stage} = scalar.cmpi slt, %stage{stage}, %bound : i32\n  '+', '.join(f'%r{stage}_{j}' for j in range(n))+f' = scf.if %active{stage} -> ({types}) {{\n'
 for j in range(n):body+=f'    %a{stage}_{j} = scalar.addi %{old}{j}, %{old}{(j+1)%n} : i32\n'
 body+='    scf.yield '+', '.join(f'%a{stage}_{j}' for j in range(n))+' : '+types+'\n  } else {\n    scf.yield '+', '.join(f'%{old}{j}' for j in range(n))+' : '+types+'\n  }\n';old=f'r{stage}_'
body+='  %expected = scalar.shli %one_i, %bound : i32\n'
s=s[:start]+body+re.sub(r'%r(\d+)',r'%r3_\1',s[end:]);Path(sys.argv[2]).write_text('// Four unrolled predicated updates; no source loop or explicit spills.\n'+s[s.index('kernel.def'):])
