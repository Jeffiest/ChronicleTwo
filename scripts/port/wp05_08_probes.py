from pathlib import Path
import json, os, shlex, subprocess, sys

repo = Path(__file__).resolve().parents[2]
build = Path('E:/Chronicle-Two/win/build-rel').resolve()
workers_dir = Path('E:/Chronicle-Two/workers').resolve()

db = json.loads((build / 'compile_commands.json').read_text())

def run(args, cwd, log_path):
    r = subprocess.run(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log_path.write_text(r.stdout, encoding='utf-8')
    print(f'{log_path.name} exit {r.returncode}', flush=True)
    if r.returncode:
        print(r.stdout[-3000:])
        raise SystemExit(r.returncode)

def get_compiler_and_flags(original_file):
    e = next(e for e in db if Path(e['file']).resolve() == (repo / original_file).resolve())
    args = shlex.split(e['command'], posix=False)
    args = [a[1:-1] if a.startswith('"') and a.endswith('"') else a for a in args]
    compiler = args[0]
    cleaned = []
    skip = False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a == '-o':
            skip = True
            continue
        if a == '-c' or Path(a).resolve() == (repo / original_file).resolve():
            continue
        cleaned.append(a)
    return compiler, cleaned, e['directory']

def compile_source(source_path, out_obj, original_ref, extra_flags=None):
    compiler, flags, directory = get_compiler_and_flags(original_ref)
    if extra_flags:
        flags.extend(extra_flags)
    log_file = out_obj.parent / (out_obj.stem + '-compile.log')
    run([compiler, *flags, '-UNDEBUG', '-c', str(source_path), '-o', str(out_obj)], directory, log_file)
    return compiler

# Parse build.ninja for link objects, flags, and libs
text = (build / 'build.ninja').read_text()
start = text.index('build darkchronicle.exe:')
block = text[start:text.index('\n\n', start)]
objects = block.splitlines()[0].split(': ', 1)[1].split()[1:]
objects = objects[:next((i for i, v in enumerate(objects) if v.startswith('|')), len(objects))]
variables = dict(line.strip().split(' = ', 1) for line in block.splitlines()[1:] if ' = ' in line)
libs = shlex.split(variables['LINK_LIBRARIES'], posix=False)
flags = shlex.split(variables['LINK_FLAGS'], posix=False)

def run_probe(task_num, test_name, extra_replacements=None):
    out = workers_dir / f'runtime-wp{task_num:02d}'
    out.mkdir(parents=True, exist_ok=True)
    print(f'\n=== Running Probe for Task {task_num:02d} ({test_name}) ===')

    test_src = repo / f'port/src/tests/wp{task_num:02d}_{test_name}_standalone.cpp'
    test_obj = out / f'wp{task_num:02d}_{test_name}.obj'
    compiler = compile_source(test_src, test_obj, 'port/src/main.cpp')

    replacements = {
        '/darkchronicle.dir/src/main.cpp.obj': str(test_obj)
    }
    if extra_replacements:
        replacements.update(extra_replacements)

    inputs = []
    for value in objects:
        norm = value.replace('\\', '/')
        replaced = False
        for suffix, target_obj in replacements.items():
            if norm.endswith(suffix):
                inputs.append(target_obj)
                replaced = True
                break
        if not replaced:
            inputs.append(value)

    exe = out / f'wp{task_num:02d}-{test_name}.exe'
    rsp = out / f'wp{task_num:02d}-{test_name}.rsp'
    cmdline = [a.replace('\\', '/') for a in [*inputs, *flags, *libs, '-o', str(exe)]]
    rsp.write_text(subprocess.list2cmdline(cmdline), encoding='utf-8')
    run([compiler, '@' + str(rsp)], build, out / f'wp{task_num:02d}-{test_name}-link.log')

    # Run the compiled test
    run_log = out / f'wp{task_num:02d}-{test_name}-run.log'
    env = os.environ.copy()
    env['PATH'] = 'E:\\Chronicle-Two\\win\\build-rel;E:\\Chronicle-Two\\win\\deps\\llvm\\llvm-mingw-20260922-ucrt-x86_64\\bin;' + env.get('PATH', '')
    r = subprocess.run([str(exe), str(repo / 'data')], cwd=out, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    run_log.write_text(r.stdout, encoding='utf-8')
    print(f'wp{task_num:02d}-{test_name} run exit {r.returncode}')
    print(r.stdout)
    if r.returncode != 0:
        raise SystemExit(r.returncode)

if __name__ == '__main__':
    # Probe Task 05
    # First recompile modified mg_drawprim.cpp into isolated dir
    out05 = workers_dir / 'runtime-wp05'
    out05.mkdir(parents=True, exist_ok=True)
    drawprim_obj = out05 / 'mg_drawprim.obj'
    compile_source(repo / 'port/src/mg_drawprim.cpp', drawprim_obj, 'port/src/mg_drawprim.cpp')
    run_probe(5, 'image_copy', {'/ct_port.dir/src/mg_drawprim.cpp.obj': str(drawprim_obj)})

    # Probe Task 06
    out06 = workers_dir / 'runtime-wp06'
    out06.mkdir(parents=True, exist_ok=True)
    texture_obj = out06 / 'mg_texture.obj'
    compile_source(repo / 'port/src/mg_texture.cpp', texture_obj, 'port/src/mg_texture.cpp')
    run_probe(6, 'atlas_storage', {'/ct_port.dir/src/mg_texture.cpp.obj': str(texture_obj)})

    # Probe Task 07
    out07 = workers_dir / 'runtime-wp07'
    out07.mkdir(parents=True, exist_ok=True)
    resources_obj = out07 / 'resources.obj'
    compile_source(repo / 'port/src/host_overrides/gfx/resources.cpp', resources_obj, 'port/src/host_overrides/gfx/resources.cpp')
    run_probe(7, 'texture_lifetime', {'/ct_host.dir/src/host_overrides/gfx/resources.cpp.obj': str(resources_obj)})

    # Probe Task 08
    out08 = workers_dir / 'runtime-wp08'
    out08.mkdir(parents=True, exist_ok=True)
    sprite_obj = out08 / 'mg_sprite.obj'
    compile_source(repo / 'port/src/mg_sprite.cpp', sprite_obj, 'port/src/mg_sprite.cpp')
    run_probe(8, 'hud_state', {
        '/ct_port.dir/src/mg_drawprim.cpp.obj': str(drawprim_obj),
        '/ct_port.dir/src/mg_sprite.cpp.obj': str(sprite_obj)
    })

    print('\nALL PROBES PASSED SUCCESSFULLY!')
