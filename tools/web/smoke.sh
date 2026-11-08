#!/usr/bin/env bash
# Smoke runs of the web build without game data (docs/WEB.md): `tools/web/smoke.sh [node|browser|all] [options]`.
#
#   node      runs darkchronicle.js under Node, headless and offscreen for 30 frames: the runtime, WasmFS, the fibers
#             and the host's setup up to the game data check, which reports the missing data and exits with 3
#             (kExitNoData, port/src/exitcodes.hpp).
#   browser   serves the build (tools/web/serve.py, COOP/COEP) and runs port/web/test/browser_smoke.mjs in headless
#             Chromium on SwiftShader: the page isolates, stores a stand-in data.zip in the origin private file system,
#             starts the game, and the game exits with 3 on the page. --renderer gl runs darkchronicle_gl.js
#             (WebGL2) instead of darkchronicle.js (WebGPU); --with-marker stores a current data.json beside the
#             archive too, so the game gets past the data check to opening the archive.
#
# Options: --build DIR (default port/build/web), --out DIR (logs; default <build>/smoke), --renderer webgpu|gl,
#          --frames N (default 30), --with-marker. The exit status is 0 when every run passed.
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
mode=all
build=$root/port/build/web
out=
renderer=webgpu
frames=30
marker=0
while [[ $# -gt 0 ]]; do
    case $1 in
        node|browser|all) mode=$1 ;;
        --build) build=$2; shift ;;
        --out) out=$2; shift ;;
        --renderer) renderer=$2; shift ;;
        --frames) frames=$2; shift ;;
        --with-marker) marker=1 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
out=${out:-$build/smoke}
mkdir -p "$out"
game=darkchronicle.js
[[ $renderer == gl ]] && game=darkchronicle_gl.js
if [[ ! -f $build/$game ]]; then
    echo "no $game in $build: build it first (tools/web/build.sh)" >&2
    exit 1
fi
node=${CT_NODE:-node}
failed=0

check_log() {
    # $1 log, $2 exit code: the exit code is 0 or 3 and the log has the markers of a run that got to the data check,
    # with none of a crash.
    local log=$1 code=$2 ok=1
    if [[ $code != 0 && $code != 3 ]]; then
        echo "  exit code $code (expected 0 or 3)"; ok=0
    fi
    if ! grep -q "session .*: Dark Chronicle" "$log"; then
        echo "  no session header in the log"; ok=0
    fi
    if [[ $code == 3 ]] && ! grep -q "No current Dark Chronicle data" "$log"; then
        echo "  exit 3 without the missing-data message"; ok=0
    fi
    if grep -q "abort:\|RuntimeError\|pageerror\|signature mismatch" "$log"; then
        echo "  fatal pattern in the log:"; grep -m 3 "abort:\|RuntimeError\|pageerror\|signature mismatch" "$log" | sed 's/^/    /'; ok=0
    fi
    [[ $ok == 1 ]]
}

if [[ $mode == node || $mode == all ]]; then
    echo "== node $game --headless --offscreen --frames $frames"
    mkdir -p "$out/node-save"
    (cd "$build" && timeout 300 "$node" "$game" --headless --offscreen --frames "$frames" --save "$out/node-save" \
        > "$out/node.log" 2>&1)
    code=$?
    echo "  exit=$code ($(wc -l < "$out/node.log") lines in $out/node.log)"
    if check_log "$out/node.log" "$code"; then echo "  node smoke: pass"; else echo "  node smoke: FAIL"; failed=1; fi
fi

if [[ $mode == browser || $mode == all ]]; then
    echo "== Chromium: $game on $renderer, --headless --offscreen --frames $frames"
    if ! "$node" -e "require('playwright')" 2> /dev/null; then
        echo "  Playwright is not installed for $node: browser smoke not run"; exit 1
    fi
    python3 - "$build" <<'PY'
import json, sys, zipfile
build = sys.argv[1]
with zipfile.ZipFile(build + "/smoke-data.zip", "w") as archive:
    archive.writestr("data/smoke.txt", "not the game data\n")
PY
    marker_url=
    if [[ $marker == 1 ]]; then
        version=$(grep -o 'KAZAROV_DATA_VERSION[[:space:]]*[0-9]*' "$root/tools/kazarov/include/kazarov/data_version.hpp" | grep -o '[0-9]*$')
        printf '{"schema_version": 1, "data_version": %s}\n' "${version:-0}" > "$build/smoke-data.json"
        marker_url=/smoke-data.json
    fi
    CT_SERVE_QUIET=1 python3 "$root/tools/web/serve.py" --port 0 "$build" > "$out/serve.log" 2>&1 &
    server=$!
    for _ in $(seq 50); do
        port=$(sed -n 's/.*:\([0-9]*\)\/$/\1/p' "$out/serve.log" | head -1)
        [[ -n $port ]] && break
        sleep 0.2
    done
    if [[ -z $port ]]; then
        echo "  the server did not start: $(cat "$out/serve.log")"; kill $server 2> /dev/null; exit 1
    fi
    url="http://127.0.0.1:$port/index.html?renderer=$renderer"
    log=$out/browser-$renderer.log
    (cd "$root/port/web/test" && timeout 600 "$node" browser_smoke.mjs "$url" /smoke-data.zip "$marker_url" "$log" \
        --headless --offscreen --frames "$frames" > "$out/browser-$renderer.driver.log" 2>&1)
    driver=$?
    kill $server 2> /dev/null
    code=$(grep -o '^exit=-\?[0-9]*' "$log" 2> /dev/null | tail -1 | cut -d= -f2)
    echo "  driver exit=$driver, game exit=${code:-none} ($log)"
    grep -m 1 '^darkchronicle .*(' "$log" | sed 's/^/  /'
    if [[ $driver == 0 && -n $code ]] && check_log "$log" "$code"; then
        echo "  browser smoke ($renderer): pass"
    else
        echo "  browser smoke ($renderer): FAIL"; failed=1
    fi
fi
exit $failed
