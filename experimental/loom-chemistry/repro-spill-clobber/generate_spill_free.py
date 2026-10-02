import argparse
from pathlib import Path

def generate(n,kind,bits,wave):
 ty=f'i{bits}';ts=', '.join([ty]*n);size=n*wave
 a=[];emit=a.append
 emit(f'''kernel.def @rotate() {{
 %one = index.constant 1 : index
 %width = index.constant {wave} : index
 kernel.launch.config workgroups(%one,%one,%one) workgroup_size(%width,%one,%one) : index
}} launch(%input: buffer,%output: buffer,%mode: buffer) {{
 %lane = kernel.workitem.id<x> : index
 %base = index.constant 0 : offset
 %iv = buffer.view %input[%base] : buffer -> view<{size}x{ty}>
 %ov = buffer.view %output[%base] : buffer -> view<{size}x{ty}>
 %mv = buffer.view %mode[%base] : buffer -> view<1xi32>
 %flag = view.load %mv[0] : view<1xi32> -> i32
 %li = index.cast %lane : index to i32
 %three = scalar.constant 3 : i32
 %class = scalar.andi %li,%three : i32
 %eff = scalar.muli %class,%flag : i32
 %four = scalar.constant 4 : i32
 %bound = scalar.subi %four,%eff : i32
 %zero = scalar.constant 0 : i32
 %one_i = scalar.constant 1 : i32
 %stride = index.constant {n} : index
 %row = index.mul %lane,%stride : index''')
 for j in range(n):emit(f' %j{j} = index.constant {j} : index\n %ix{j} = index.add %row,%j{j} : index\n %v{j} = view.load %iv[%ix{j}] : view<{size}x{ty}> -> {ty}')
 ns=lambda p:', '.join(f'%{p}{j}' for j in range(n))
 emit(f" %iterations, {ns('r')} = scf.while(%i = %zero : i32, "+', '.join(f'%x{j} = %v{j} : {ty}' for j in range(n))+f') -> (i32, {ts}) {{\n %go = scalar.cmpi slt,%i,%bound : i32\n scf.condition %go,%i, '+ns('x')+f' : i1,i32,{ts}\n }} do(%k: i32, '+', '.join(f'%b{j}: {ty}' for j in range(n))+') {\n %next = scalar.addi %k,%one_i : i32')
 left=[(j+1)%n for j in range(n)];right=[(j-1)%n for j in range(n)]
 if kind=='rotate':values=', '.join(f'%b{j}' for j in left)
 else:
  emit(' %bit = scalar.andi %k,%one_i : i32\n %even = scalar.cmpi eq,%bit,%zero : i32')
  emit(f" {ns('y')} = scf.if %even -> ({ts}) {{\n scf.yield "+', '.join(f'%b{j}' for j in left)+f' : {ts}\n }} else {{')
  if kind=='alternate':emit(' scf.yield '+', '.join(f'%b{j}' for j in right)+f' : {ts}')
  elif kind=='mix':
   for j in range(n):emit(f' %u{j} = scalar.addi %b{j},%b{left[j]} : {ty}')
   emit(' scf.yield '+ns('u')+f' : {ts}')
  elif kind=='select':
   emit(' %two = scalar.constant 2 : i32\n %small = scalar.cmpi slt,%class,%two : i32')
   for j in range(n):emit(f' %u{j} = scf.select %small,%b{left[j]},%b{right[j]} : {ty}')
   emit(' scf.yield '+ns('u')+f' : {ts}')
  emit(' }');values=ns('y')
 emit(f' scf.yield %next,{values} : i32,{ts}\n }}')
 for c in range(3):emit(f' %c{c} = scalar.constant {c} : i32\n %is{c} = scalar.cmpi eq,%class,%c{c} : i32')
 emit(' %uniform = scalar.cmpi eq,%flag,%zero : i32')
 def oracle(c,mode):
  v=list(range(1,n+1))
  for k in range(4-c*mode):
   if kind=='rotate' or k%2==0:v=[v[j] for j in left]
   elif kind=='alternate':v=[v[j] for j in right]
   elif kind=='mix':v=[v[j]+v[left[j]] for j in range(n)]
   else:v=[v[j] for j in (left if c<2 else right)]
  return v
 for j in range(n):
  for mode in [0,1]:
   for c in range(4):emit(f' %e{mode}_{c}_{j} = scalar.constant {oracle(c,mode)[j]} : {ty}')
   emit(f' %e23_{mode}_{j} = scf.select %is2,%e{mode}_2_{j},%e{mode}_3_{j} : {ty}\n %e123_{mode}_{j} = scf.select %is1,%e{mode}_1_{j},%e23_{mode}_{j} : {ty}\n %case{mode}_{j} = scf.select %is0,%e{mode}_0_{j},%e123_{mode}_{j} : {ty}')
  emit(f' %expected{j} = scf.select %uniform,%case0_{j},%case1_{j} : {ty}\n %diff{j} = scalar.subi %r{j},%expected{j} : {ty}\n view.store %diff{j},%ov[%ix{j}] : {ty},view<{size}x{ty}>')
 emit(' kernel.return\n}')
 for name,mode in [('mixed',1),('uniform',0)]:emit(f'''check.case @{name} {{
 %input = check.generate.iota offset(1) step(1) period({n}) : tensor<{size}x{ty}>
 %output = check.generate.fill value(-1) : tensor<{size}x{ty}>
 %mode = check.generate.fill value({mode}) : tensor<1xi32>
 %expected = check.generate.fill value(0) : tensor<{size}x{ty}>
 kernel.launch @rotate(%input,%output,%mode) : (tensor<{size}x{ty}>,tensor<{size}x{ty}>,tensor<1xi32>)
 check.expect.equal actual(%output) expected(%expected) : tensor<{size}x{ty}>
 check.return
}}''')
 return '\n'.join(a)+'\n'
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind',choices=['rotate','alternate','mix','select']);p.add_argument('width',type=int);p.add_argument('bits',type=int);p.add_argument('wave',type=int);p.add_argument('output',type=Path);o=p.parse_args();o.output.write_text(generate(o.width,o.kind,o.bits,o.wave))
