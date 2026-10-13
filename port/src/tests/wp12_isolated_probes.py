from pathlib import Path
import json, os, shlex, subprocess, sys
repo=Path(__file__).resolve().parents[3]
build=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve(); out.mkdir(parents=True,exist_ok=True)
db=json.loads((build/'compile_commands.json').read_text())
def run(args,cwd,log):
 r=subprocess.run(args,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (out/log).write_text(r.stdout,encoding='utf-8'); print(log,r.returncode,flush=True)
 if r.returncode: print(r.stdout[-5000:]); raise SystemExit(r.returncode)
def compile(source,output,original):
 e=next(e for e in db if Path(e['file']).resolve()==(repo/original).resolve())
 args=shlex.split(e['command'],posix=False)
 args=[a[1:-1] if a.startswith('"') and a.endswith('"') else a for a in args]
 compiler=args[0]; cleaned=[]; skip=False
 for a in args[1:]:
  if skip: skip=False; continue
  if a=='-o': skip=True; continue
  if a=='-c' or Path(a).resolve()==Path(e['file']).resolve(): continue
  cleaned.append(a)
 run([compiler,*cleaned,'-UNDEBUG','-c',str(source),'-o',str(output)],e['directory'],output.stem+'-compile.log')
 return compiler
text=(build/'build.ninja').read_text()
start=text.index('build darkchronicle.exe:'); block=text[start:text.index('\n\n',start)]
objects=block.splitlines()[0].split(': ',1)[1].split()[1:]
objects=objects[:next((i for i,v in enumerate(objects) if v.startswith('|')),len(objects))]
variables=dict(line.strip().split(' = ',1) for line in block.splitlines()[1:] if ' = ' in line)
libs=shlex.split(variables['LINK_LIBRARIES'],posix=False)
flags=shlex.split(variables['LINK_FLAGS'],posix=False)
sound_obj=out/'sound-fixed.obj'
compiler=compile(repo/'port/src/sound.cpp',sound_obj,'port/src/sound.cpp')
baseline=out/'sound-before.cpp'
r=subprocess.run(['git','-c','safe.directory='+repo.as_posix(),'show','HEAD:port/src/sound.cpp'],cwd=repo,stdout=subprocess.PIPE,check=True)
baseline.write_bytes(r.stdout)
before_obj=out/'sound-before.obj'; compile(baseline,before_obj,'port/src/sound.cpp')
for name in ['sound_bridge','weighted_models']:
 obj=out/(name+'.obj'); compile(repo/('port/src/tests/wp12_'+name+'_standalone.cpp'),obj,'port/src/main.cpp')
 variants=['before','fixed'] if name=='sound_bridge' else ['fixed']
 for variant in variants:
  inputs=[]
  for value in objects:
   normalized=value.replace('\\','/')
   if normalized.endswith('/darkchronicle.dir/src/main.cpp.obj'): inputs.append(str(obj))
   elif normalized.endswith('/ct_port.dir/src/sound.cpp.obj'): inputs.append(str(before_obj if variant=='before' else sound_obj))
   else: inputs.append(value)
  exe=out/(name+'-'+variant+'.exe'); rsp=out/(name+'-'+variant+'.rsp')
  rsp.write_text(subprocess.list2cmdline([a.replace('\\','/') for a in [*inputs,*flags,*libs,'-o',str(exe)]]),encoding='utf-8')
  run([compiler,'@'+str(rsp)],build,name+'-'+variant+'-link.log')
  r=subprocess.run([str(exe),str(repo/'data')],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
  (out/(name+'-'+variant+'-run.log')).write_text(r.stdout,encoding='utf-8')
  print(name,variant,'exit',r.returncode, r.stdout[-2500:],flush=True)
  if variant=='before':
   if r.returncode==0: raise SystemExit('baseline Exit unexpectedly passed')
  elif r.returncode: raise SystemExit(r.returncode)
