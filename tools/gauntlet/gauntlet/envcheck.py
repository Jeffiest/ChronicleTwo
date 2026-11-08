"""G0 environment checks: the toolchain DC2_PLAN.md "Building here" names, from tools/gauntlet/env.sh.

check(env) returns a list of {name, required, ok, detail}; a required check that fails fails the stage.
Optional tools (lavapipe, emcc, Playwright, PIL, xvfb-run, lua) are reported so later gates can say
why they did not run.
"""
import os
import re
import subprocess
import sys

from . import util


def _output(cmd, env=None):
    try:
        merged = dict(os.environ)
        merged.update(env or {})
        out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60, env=merged)
        return out.returncode, out.stdout.decode("utf-8", "replace").strip()
    except (OSError, subprocess.TimeoutExpired) as e:
        return 127, str(e)


def _version(cmd, pattern, env=None):
    code, text = _output(cmd, env)
    match = re.search(pattern, text)
    return (match.group(1) if match else None), text.splitlines()[0] if text else ""


def _ffmpeg_major(env):
    include = os.path.join(env.get("CT_FFMPEG_DIR", "/opt/ffmpeg-sys"), "include", "libavformat", "version_major.h")
    try:
        with open(include) as f:
            match = re.search(r"LIBAVFORMAT_VERSION_MAJOR\s+(\d+)", f.read())
            if match:
                return match.group(1), include
    except OSError:
        pass
    code, text = _output(["pkg-config", "--modversion", "libavformat"])
    return (text.split(".")[0] if code == 0 else None), "pkg-config"


def check(env):
    checks = []

    def add(name, required, ok, detail):
        checks.append({"name": name, "required": required, "ok": bool(ok), "detail": detail})

    cc = env.get("CT_CC", "clang-20")
    cxx = env.get("CT_CXX", "clang++-20")
    want_clang = env.get("CT_CLANG_VERSION", "20")
    version, line = _version([cc, "--version"], r"clang version (\d+)")
    add("clang", True, version == want_clang, line or "%s not found" % cc)
    probe = subprocess.run([cxx, "-stdlib=libc++", "-x", "c++", "-fsyntax-only", "-"], input=b"#include <string>\nint main(){}\n",
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60) if version else None
    add("libc++", True, probe is not None and probe.returncode == 0,
        "compiles <string> with -stdlib=libc++" if probe is not None and probe.returncode == 0 else
        (probe.stdout.decode("utf-8", "replace")[:200] if probe is not None else "no compiler"))
    version, line = _version([env.get("CT_LLD", "ld.lld-20"), "--version"], r"LLD (\d+)")
    add("lld", True, version == want_clang, line or "ld.lld not found")
    version, line = _version(["cmake", "--version"], r"version (\d+\.\d+)")
    add("cmake", True, version is not None and tuple(int(v) for v in version.split(".")) >= (3, 21), line)
    version, line = _version(["ninja", "--version"], r"(\d+\.\d+)")
    add("ninja", True, version is not None, line)
    want_rust = env.get("CT_RUST_VERSION", "1.99")
    version, line = _version(["rustc", "--version"], r"rustc (\d+\.\d+)")
    add("rustc", True, version == want_rust, line or "rustc not found")
    version, line = _version(["cargo", "--version"], r"cargo (\d+\.\d+)")
    add("cargo", True, version is not None, line)
    want_sdl = env.get("CT_SDL3_VERSION", "3.4.18")
    code, text = _output(["pkg-config", "--modversion", "sdl3"])
    sdl_dir = env.get("CT_SDL3_DIR", "")
    sdl_ok = (code == 0 and text == want_sdl) or os.path.exists(os.path.join(sdl_dir, "SDL3ConfigVersion.cmake"))
    add("sdl3", True, sdl_ok, "pkg-config %s; %s" % (text if code == 0 else "none", sdl_dir))
    headers = env.get("CT_VULKAN_HEADERS", "")
    core = os.path.join(headers, "vulkan", "vulkan_core.h")
    header_version = None
    try:
        with open(core) as f:
            match = re.search(r"#define VK_HEADER_VERSION\s+(\d+)", f.read())
            header_version = match.group(1) if match else None
    except OSError:
        pass
    add("vulkan-headers", True, header_version is not None, "%s (1.4.%s)" % (headers, header_version)
        if header_version else "%s has no vulkan/vulkan_core.h" % headers)
    add("vulkan-loader", True, os.path.exists("/usr/lib/x86_64-linux-gnu/libvulkan.so") or util.which("vulkaninfo"),
        "/usr/lib/x86_64-linux-gnu/libvulkan.so")
    glslang = util.which("glslangValidator")
    version, line = _version([glslang or "glslangValidator", "--version"], r"Version: ([\d:.]+)")
    add("glslangValidator", True, glslang is not None, line or "not on PATH")
    icd = env.get("CT_LAVAPIPE_ICD", "")
    lavapipe = None
    if os.path.exists(icd):
        code, text = _output(["vulkaninfo", "--summary"], {"VK_ICD_FILENAMES": icd, "VK_DRIVER_FILES": icd})
        match = re.search(r"deviceName\s*=\s*(.+)", text)
        lavapipe = match.group(1).strip() if (code == 0 and match) else None
    add("lavapipe", False, lavapipe is not None, lavapipe or "%s absent or vulkaninfo found no device" % icd)
    major, where = _ffmpeg_major(env)
    add("libavformat", True, major is not None, "libavformat major %s (%s); Kazarov's movie_mux.c needed a typedef "
        "for FFmpeg 6.1's non-const write callback" % (major, where) if major else "no libavformat headers")
    ffmpeg_lib = os.path.join(env.get("CT_FFMPEG_DIR", "/opt/ffmpeg-sys"), "lib")
    missing = [n for n in ("avutil", "swresample", "swscale", "avcodec", "avformat")
               if not os.path.exists(os.path.join(ffmpeg_lib, "lib%s.a" % n))]
    add("ffmpeg-tree", True, not missing, "%s: all five archives (linker scripts)" % ffmpeg_lib if not missing
        else "%s missing %s (run ct_ffmpeg_tree from env.sh)" % (ffmpeg_lib, ", ".join(missing)))
    add("CXXFLAGS", True, "libc++" in env.get("CXXFLAGS", "") and env.get("CXXSTDLIB") == "c++",
        "env.sh exports CXXFLAGS=%r CXXSTDLIB=%r for kazarov_core's cc-rs build" % (env.get("CXXFLAGS"), env.get("CXXSTDLIB")))
    deps = env.get("CT_DEPS", "/opt/deps")
    missing = [n for n in ("corrosion", "simdjson", "fastgltf", "luau", "quill", "cpptrace", "libdwarf", "tracy",
                           "googletest") if not os.path.isdir(os.path.join(deps, n))]
    add("fetchcontent-sources", True, not missing, deps + (" missing " + ", ".join(missing) if missing else ""))
    emcc = os.path.join(env.get("CT_EMSDK", ""), "emcc")
    want_emcc = env.get("CT_EMCC_VERSION", "6.0.13")
    version, line = _version([emcc, "--version"], r"emcc \(.*?\) (\d+\.\d+\.\d+)", {"EM_CONFIG": env.get("EM_CONFIG", "")})
    add("emcc", False, version == want_emcc, "%s (EM_CONFIG=%s)" % (line or emcc + " not found", env.get("EM_CONFIG")))
    node = env.get("CT_NODE") if os.path.exists(env.get("CT_NODE", "")) else util.which("node")
    version, line = _version([node or "node", "--version"], r"v(\d+)")
    add("node", True, version is not None and int(version) >= 20, line or "node not found")
    code, text = _output([node or "node", "-e", "require('playwright'); console.log('playwright ok')"])
    add("playwright", False, code == 0 and "playwright ok" in text, text.splitlines()[-1] if text else "")
    add("xvfb-run", False, util.which("xvfb-run") is not None, util.which("xvfb-run") or "not on PATH")
    code, text = _output([sys.executable, "-I", "-c", "import PIL; print(PIL.__version__)"])
    add("PIL", False, code == 0, "PIL %s" % text if code == 0 else "not installed: pure-Python PNG reader")
    lua = util.which("lua", "lua5.5", "lua5.4")
    add("lua", False, lua is not None, lua or "none on PATH: MoonMod's vendored 5.5.1 is built into .cache")
    add("python", True, sys.version_info >= (3, 10), sys.version.split()[0])
    return checks


def summary(checks):
    failed = [c["name"] for c in checks if c["required"] and not c["ok"]]
    missing = [c["name"] for c in checks if not c["required"] and not c["ok"]]
    parts = ["%d checks" % len(checks)]
    if failed:
        parts.append("required failing: " + ", ".join(failed))
    if missing:
        parts.append("optional absent: " + ", ".join(missing))
    return "; ".join(parts), not failed
