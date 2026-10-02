import argparse, importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('high',Path(__file__).with_name('generate_high_pressure.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def generate(n,kind):
 s=m.generate(n)
 start=s.index('  %iterations,'); body=s.index('    %two =',start);stop=s.index('    scf.yield %next,',body)
 inner=s[body:stop]
 exp=s.index('  %tw =',stop); stores=s.index('  %diff0',exp)
 if kind in ['branch','nested_branch','masked_update','select','early_return']:
  inner=inner.replace('%li, %two','%effective_lane, %two')
  import re
  inner=re.sub(r'%b(\d+)',r'%v\1',inner)
  if kind=='masked_update':
   cut=inner.index('    } else {'); inner=inner[:cut]+'    } else {\n      scf.yield '+', '.join(f'%v{j}' for j in range(n))+' : '+', '.join(['i32']*n)+'\n    }\n'
  if kind=='nested_branch':
   inner=re.sub(r'%y(\d+)',r'%inner\1',inner)
   wrapped='  %three = scalar.constant 3 : i32\n  %outer = scalar.cmpi slt, %effective_lane, %three : i32\n  '+', '.join(f'%y{j}' for j in range(n))+' = scf.if %outer -> ('+', '.join(['i32']*n)+') {\n'+inner
   wrapped+='    scf.yield '+', '.join(f'%inner{j}' for j in range(n))+' : '+', '.join(['i32']*n)+'\n  } else {\n    %eight = scalar.constant 8 : i32\n'
   for j in range(n):wrapped+=f'    %outside{j} = scalar.muli %v{j}, %eight : i32\n'
   wrapped+='    scf.yield '+', '.join(f'%outside{j}' for j in range(n))+' : '+', '.join(['i32']*n)+'\n  }\n';inner=wrapped
  if kind=='select':
   inner='  %two = scalar.constant 2 : i32\n  %branch = scalar.cmpi slt, %effective_lane, %two : i32\n'
   for j in range(n):inner+=f'  %a{j} = scalar.addi %v{j}, %v{(j+1)%n} : i32\n  %y{j} = scf.select %branch, %a{j}, %v{j} : i32\n'
  expectation='  %two_e = scalar.constant 2 : i32\n  %choose = scalar.cmpi slt, %effective_lane, %two_e : i32\n'
  expectation+='  %expected = scf.select %choose, %two_e, '+('%four_i' if kind in ['branch','early_return','nested_branch'] else '%one_i')+' : i32\n'
  if kind=='nested_branch':
   expectation=expectation.replace('%expected =', '%inside_expected =')+'  %eight_e = scalar.constant 8 : i32\n  %expected = scf.select %outer, %inside_expected, %eight_e : i32\n'
  suffix=re.sub(r'%r(\d+)',r'%y\1',s[stores:])
  if kind=='early_return':
   prefix='  %two_return = scalar.constant 2 : i32\n  %return_now = scalar.cmpi slt, %effective_lane, %two_return : i32\n  cfg.cond_br %return_now, ^early, ^work\n^early:\n'
   for j in range(n):prefix+=f'  %early{j} = scalar.subi %v{j}, %one_i : i32\n  view.store %early{j}, %ov[%ix{j}] : i32, view<{n*4}xi32>\n'
   prefix+='  kernel.return\n^work:\n';inner=prefix+inner
  s=s[:start]+inner+expectation+suffix
 elif kind=='break':
  args=', '.join(['%zero: i32']+[f'%v{j}: i32' for j in range(n)])
  params=', '.join(['%k: i32']+[f'%b{j}: i32' for j in range(n)])
  prefix=f'  cfg.br ^header({args})\n^header({params}):\n  %under_limit = scalar.cmpi slt, %k, %four_i : i32\n  cfg.cond_br %under_limit, ^body, ^exit\n^body:\n  %break_now = scalar.cmpi sge, %k, %bound : i32\n  cfg.cond_br %break_now, ^exit, ^work\n^work:\n  %next = scalar.addi %k, %one_i : i32\n'
  back='  cfg.br ^header('+', '.join(['%next: i32']+[f'%y{j}: i32' for j in range(n)])+')\n^exit:\n'
  import re
  s=s[:start]+prefix+inner+back+re.sub(r'%r(\d+)',r'%b\1',s[exp:])
 s='// Generated high-level IR diagnostic: '+kind+'; no explicit spills.\n'+s[s.index('kernel.def'):]
 return s.replace('@divergent_pressure',f'@{kind}_divergent').replace('@uniform_trip_count',f'@{kind}_uniform')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('kind', choices=['branch','nested_branch','masked_update','select','break','early_return']);p.add_argument('width',type=int);p.add_argument('out',type=Path);a=p.parse_args();a.out.write_text(generate(a.width,a.kind))
