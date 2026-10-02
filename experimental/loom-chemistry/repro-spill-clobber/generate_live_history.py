import argparse
from pathlib import Path

def generate(n,kind,stages):
 lines=[];a=lines.append
 a(f'''kernel.def @stress() {{
 %one = index.constant 1 : index
 %four = index.constant 4 : index
 kernel.launch.config workgroups(%one,%one,%one) workgroup_size(%four,%one,%one) : index
}} launch(%input: buffer,%output: buffer,%mode: buffer) {{
 %lane = kernel.workitem.id<x> : index
 %base = index.constant 0 : offset
 %iv = buffer.view %input[%base] : buffer -> view<{4*n}xi32>
 %ov = buffer.view %output[%base] : buffer -> view<{4*n*stages}xi32>
 %mv = buffer.view %mode[%base] : buffer -> view<1xi32>
 %flag = view.load %mv[0] : view<1xi32> -> i32
 %li = index.cast %lane : index to i32
 %eff = scalar.muli %li, %flag : i32
 %stride = index.constant {n} : index
 %row = index.mul %lane, %stride : index''')
 for j in range(n):a(f' %j{j} = index.constant {j} : index\n %ix{j} = index.add %row,%j{j} : index\n %v{j} = view.load %iv[%ix{j}] : view<{4*n}xi32> -> i32')
 history=[];oracle=[[j+1 for j in range(n)] for _ in range(4)];old='v';ty=', '.join(['i32']*n)
 for stage in range(stages):
  # Cross-cutting masks: lanes can leave and re-enter regions, without loops.
  masks=[[True,True,False,False],[True,False,True,False],[False,True,True,True],[True,True,True,False]]
  truth=masks[stage%4]
  if stage%4==1:
   a(f' %bit{stage} = scalar.constant 1 : i32\n %low{stage} = scalar.andi %eff,%bit{stage} : i32\n %zero{stage} = scalar.constant 0 : i32\n %cond{stage} = scalar.cmpi eq,%low{stage},%zero{stage} : i32')
  else:
   bound,op=[(2,'slt'),(0,'slt'),(1,'sge'),(3,'slt')][stage%4]
   a(f' %limit{stage} = scalar.constant {bound} : i32\n %cond{stage} = scalar.cmpi {op},%eff,%limit{stage} : i32')
  new=f'r{stage}_';ap=f'a{stage}_';bp=f'b{stage}_'
  if kind!='select':a(' '+', '.join(f'%{new}{j}' for j in range(n))+f' = scf.if %cond{stage} -> ({ty}) {{')
  for j in range(n):a(f' %{ap}{j} = scalar.addi %{old}{j},%{old}{(j+1)%n} : i32')
  if kind!='select':a(' scf.yield '+', '.join(f'%{ap}{j}' for j in range(n))+' : '+ty+'\n } else {')
  if kind=='branch':
   for j in range(n):a(f' %{bp}{j} = scalar.subi %{old}{j},%{old}{(j+3)%n} : i32')
   fallback=bp
  else:fallback=old
  if kind=='select':
   for j in range(n):a(f' %{new}{j} = scf.select %cond{stage},%{ap}{j},%{old}{j} : i32')
  else:a(' scf.yield '+', '.join(f'%{fallback}{j}' for j in range(n))+' : '+ty+'\n }')
  for lane in range(4):
   prev=oracle[lane];oracle[lane]=[(prev[j]+prev[(j+1)%n]) if truth[lane] else (prev[j]-prev[(j+3)%n] if kind=='branch' else prev[j]) for j in range(n)]
  old=new
  history.append((new,[v[:] for v in oracle]))
 # Oracle is evaluated in Python, independently of generated GPU arithmetic.
 # mode=0 executes lane-zero's predicates in all lanes.
 for lane in range(3):a(f' %lane{lane} = scalar.constant {lane} : i32\n %is{lane} = scalar.cmpi eq,%eff,%lane{lane} : i32')
 for h,(version,expected_values) in enumerate(history):
  a(f' %outbase{h} = index.constant {h*4*n} : index')
  for j in range(n):
   for lane in range(4):a(f' %e{h}_{lane}_{j} = scalar.constant {expected_values[lane][j]} : i32')
   a(f' %e23_{h}_{j} = scf.select %is2,%e{h}_2_{j},%e{h}_3_{j} : i32\n %e123_{h}_{j} = scf.select %is1,%e{h}_1_{j},%e23_{h}_{j} : i32\n %expected{h}_{j} = scf.select %is0,%e{h}_0_{j},%e123_{h}_{j} : i32\n %diff{h}_{j} = scalar.subi %{version}{j},%expected{h}_{j} : i32\n %outix{h}_{j} = index.add %ix{j},%outbase{h} : index\n view.store %diff{h}_{j},%ov[%outix{h}_{j}] : i32,view<{4*n*stages}xi32>')
 a(' kernel.return\n}')
 for name,mode in [('mixed',1),('uniform',0)]:a(f'''check.case @{name} {{
 %input = check.generate.iota offset(1) step(1) period({n}) : tensor<{4*n}xi32>
 %output = check.generate.fill value(-1) : tensor<{4*n*stages}xi32>
 %mode = check.generate.fill value({mode}) : tensor<1xi32>
 %expected = check.generate.fill value(0) : tensor<{4*n*stages}xi32>
 kernel.launch @stress(%input,%output,%mode) : (tensor<{4*n}xi32>,tensor<{4*n*stages}xi32>,tensor<1xi32>)
 check.expect.equal actual(%output) expected(%expected) : tensor<{4*n*stages}xi32>
 check.return
}}''')
 return '\n'.join(lines)+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=['branch','update','select']);p.add_argument('width',type=int);p.add_argument('stages',type=int);p.add_argument('output',type=Path);o=p.parse_args();o.output.write_text(generate(o.width,o.kind,o.stages))
