#!/usr/bin/env python3
"""Two headless Chronicle Two games meet through the co-op relay.

Starts DC_Web's relay (coop/relay/server.mjs) on a free port, then runs two `darkchronicle --headless` processes on
synthetic data (the empty publisher fixture the smoke tests use: no disc, no retail data), each with a tiny fixture
mod that uses the engine's net.* through MoonMod's Lua API: the first hosts a room and keeps its code in its storage, the
second is written with that code and joins, each sends the other a message, and both log `COOP_E2E ...` markers (kept in
their storage too, since stdout may still be buffered when the script stops a game). The check
passes when both sides saw the handshake. The games stay in the logo fade (no data): only the net layer, the API
dispatch and the mod runtime are exercised; a floor or a town needs a disc.

    python3 tools/coop/two_games.py --build port/build/pc [--relay-dir ../DC_Web/coop/relay] [--timeout 180]

Exit 0 when the markers are all there, 1 when not, 2 when something could not start (reported as "not run").
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FIXTURE_HEAD = r'''
-- The two-process check's mod: host or join (ROLE), through the engine's net.*, and record what happens. A mod has
-- no io or os, so the script writes ROLE, RELAY and the room CODE into this file, and the mod keeps its markers in
-- its storage (moonmod/data/<id>/storage.json), where the script reads them; they go to the log as well.
local role = "@ROLE@"
local relay = "@RELAY@"
local room_code = "@CODE@"
local trail = {}
local function log(line)
  moonmod.log("info", "COOP_E2E " .. line)
  trail[#trail + 1] = line
  local text = table.concat(trail, "\n")
  if #text <= 4000 then moonmod.storage.set("markers", text) end
end
'''
FIXTURE_BODY = r'''
local info = game.info.get()
log("game " .. tostring(info.id))
local supported, reason = moonmod.api.supports("net.host")
log("net.host " .. (supported and "supported" or ("unsupported: " .. tostring(reason))))
if not supported then return {} end
local state = {opened = false, done = false, peer = nil, joined_at = nil}
local function open_room()
  local ok, err = pcall(function()
    local args = {version = 1, name = role == "host" and "Host" or "Guest", relay = relay}
    local result, fail
    if role == "host" then
      result, fail = game.net.host(args)
    else
      if #room_code ~= 5 then return false end
      args.code = room_code
      result, fail = game.net.join(args)
    end
    if not result then log("open failed " .. tostring(fail and fail.message)) return true end
    log("opening as " .. role)
    return true
  end)
  if not ok then log("open error " .. tostring(err)) return true end
  return err
end
moonmod.on("game.tick", function(e)
  if state.done then return end
  if not state.opened then
    if e.frame % 10 == 0 then state.opened = open_room() end
    return
  end
  local got = game.net.receive({max = 32})
  for _, ev in ipairs(got and got.events or {}) do
    if ev.kind == "connected" then
      log("connected peer " .. tostring(ev.peer) .. " code " .. tostring(ev.code))
      if role == "host" then moonmod.storage.set("code", tostring(ev.code)) end
      if role == "guest" then game.net.send({to = 0, data = "HELLO from the guest"}) end
    elseif ev.kind == "joined" then
      log("joined peer " .. tostring(ev.peer) .. " name " .. tostring(ev.name))
      game.net.send({to = ev.peer, data = "WELCOME from the host"})
    elseif ev.kind == "message" then
      log("message from " .. tostring(ev.peer) .. ": " .. tostring(ev.data))
      if role == "host" and ev.data == "HELLO from the guest" then
        log("handshake complete")
        state.done = true
        moonmod.storage.set("done", true)
      elseif role == "guest" and ev.data == "WELCOME from the host" then
        log("handshake complete")
        state.done = true
        moonmod.storage.set("done", true)
        game.net.leave()
      end
    elseif ev.kind == "left" then
      log("left peer " .. tostring(ev.peer))
    elseif ev.kind == "closed" then
      log("closed " .. tostring(ev.reason))
    end
  end
end)
return {}
'''

MOD_JSON = {
    "schema_version": 1, "id": "moonmod.coop-e2e", "name": "Co-op two-process check", "version": "1.0.0",
    "framework_api": 1, "games": ["chronicle-two"], "entry": "main.lua", "coop": "local",
    "permissions": ["game.read", "game.write", "storage.read", "storage.write", "net.session"],
}


FIXTURE_MOD = FIXTURE_HEAD + FIXTURE_BODY


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def url_ok(url):
    try:
        with urllib.request.urlopen(url, timeout=2) as r:
            return r.status < 500
    except Exception:
        return False


def find_relay(explicit):
    candidates = [explicit] if explicit else []
    candidates += [os.path.join(ROOT, "coop", "relay"), os.path.join(os.path.dirname(ROOT), "DC_Web", "coop", "relay"),
                   os.path.join(ROOT, "tools", "gauntlet", ".cache", "relay")]
    for path in candidates:
        if path and os.path.isfile(os.path.join(path, "server.mjs")) and os.path.isdir(os.path.join(path, "node_modules", "ws")):
            return path
    return None


def read_storage(save_root):
    path = os.path.join(save_root, "moonmod", "data", MOD_JSON["id"], "storage.json")
    try:
        with open(path) as f:
            value = json.load(f)
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        return {}


def write_fixture(save_root, data_root, role, relay_url, code):
    os.makedirs(os.path.join(data_root, "data"), exist_ok=True)
    version_source = os.path.join(ROOT, "tools", "kazarov", "data_version.json")
    data_version = 1
    if os.path.isfile(version_source):
        with open(version_source) as f:
            data_version = json.load(f).get("data_version", 1)
    with open(os.path.join(data_root, "data.json"), "w") as f:
        json.dump({"schema_version": 1, "data_version": data_version}, f)
    mod = os.path.join(save_root, "mods", "moonmod.coop-e2e")
    os.makedirs(mod, exist_ok=True)
    os.makedirs(os.path.join(save_root, "moonmod"), exist_ok=True)
    with open(os.path.join(mod, "mod.json"), "w") as f:
        json.dump(MOD_JSON, f)
    with open(os.path.join(mod, "main.lua"), "w") as f:
        f.write(FIXTURE_MOD.replace("@ROLE@", role).replace("@RELAY@", relay_url).replace("@CODE@", code))
    with open(os.path.join(save_root, "moonmod", "profile.json"), "w") as f:
        json.dump({"format_version": 1, "enabled": {"moonmod.coop-e2e": True}, "settings": {}}, f)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=os.path.join(ROOT, "port", "build", "pc"), help="the PC build directory (darkchronicle lives in it)")
    parser.add_argument("--relay-dir", default=None, help="the relay's directory (server.mjs with node_modules/ws)")
    parser.add_argument("--node", default=os.environ.get("CT_NODE") if os.path.exists(os.environ.get("CT_NODE", "")) else "node")
    parser.add_argument("--frames", type=int, default=1000000, help="the most frames each game runs (headless games are unpaced)")
    parser.add_argument("--timeout", type=float, default=180, help="seconds to wait for the handshake")
    parser.add_argument("--work", default=None, help="keep the fixture and logs here instead of a temporary folder")
    args = parser.parse_args()

    exe = os.path.join(args.build, "darkchronicle")
    if not os.path.isfile(exe):
        print("NOT RUN: no darkchronicle in %s (build it first)" % args.build)
        return 2
    relay_dir = find_relay(args.relay_dir)
    if relay_dir is None:
        print("NOT RUN: no relay (server.mjs with node_modules/ws); pass --relay-dir, or `npm install` in DC_Web/coop/relay")
        return 2
    work = args.work or tempfile.mkdtemp(prefix="coop-two-games-")
    os.makedirs(work, exist_ok=True)
    port = free_port()
    relay_url = "ws://127.0.0.1:%d" % port
    relay_log = open(os.path.join(work, "relay.log"), "w")
    relay = subprocess.Popen([args.node, "server.mjs", "--port", str(port), "--host", "127.0.0.1", "--quiet"], cwd=relay_dir,
                             stdout=relay_log, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            if url_ok("http://127.0.0.1:%d/health" % port):
                break
            time.sleep(0.2)
        else:
            print("NOT RUN: the relay did not answer /health on port %d" % port)
            return 2
        games = {}
        roots = {}

        def start(role, code):
            save_root = os.path.join(work, role, "save")
            data_root = os.path.join(work, role, "data-root")
            roots[role] = save_root
            write_fixture(save_root, data_root, role, relay_url, code)
            env = dict(os.environ)
            env.update({"DC2_SAVE": save_root, "DC2_DATA": os.path.join(data_root, "data"), "DC2_COOP_RELAY": relay_url, "DC_COOP_RELAY": relay_url,
                        "MOONMOD_NO_MODS": "0", "DC2_AUDIO": "off", "SDL_VIDEO_DRIVER": "offscreen"})
            log = open(os.path.join(work, role + ".log"), "w")
            command = [exe, "--headless", "--offscreen", "--frames", str(args.frames)]
            games[role] = (subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=work), log, command)

        # Headless games run unpaced, so a frame count is no clock: each runs until both have finished the handshake,
        # one exits, or the timeout passes. The guest starts once the host's mod has stored the room code.
        deadline = time.monotonic() + args.timeout
        start("host", "")
        code = ""
        while time.monotonic() < deadline and games["host"][0].poll() is None:
            code = str(read_storage(roots["host"]).get("code", ""))
            if len(code) == 5:
                break
            time.sleep(0.1)
        if len(code) == 5:
            start("guest", code)
            while time.monotonic() < deadline:
                if all(read_storage(roots[r]).get("done") is True for r in games):
                    break
                if any(process.poll() is not None for process, _, _ in games.values()):
                    break
                time.sleep(0.1)
        results = {}
        for role, (process, log, command) in games.items():
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            log.close()
            with open(log.name, errors="replace") as f:
                text = f.read()
            markers = [line.strip()[line.strip().index("COOP_E2E"):] for line in text.splitlines() if "COOP_E2E" in line]
            stored = str(read_storage(roots[role]).get("markers", ""))
            markers += ["COOP_E2E " + line for line in stored.splitlines() if "COOP_E2E " + line not in markers]
            results[role] = (process.returncode, markers, command)
        for role in ("host", "guest"):
            results.setdefault(role, (None, [], ["(not started: the host stored no room code)"]))
    finally:
        relay.terminate()
        try:
            relay.wait(10)
        except subprocess.TimeoutExpired:
            relay.kill()
        relay_log.close()

    failures = 0

    def check(ok, what):
        nonlocal failures
        print("%s %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            failures += 1

    for role in ("host", "guest"):
        code, markers, command = results[role]
        print("%s: exit %s, %d markers (%s)" % (role, code, len(markers), " ".join(command)))
        for marker in markers:
            print("    " + marker)
        joined = " ".join(markers)
        check("net.host supported" in joined, "%s: this build has online play" % role)
        check("connected peer" in joined, "%s: the relay welcomed it" % role)
    host_markers = " ".join(results["host"][1])
    guest_markers = " ".join(results["guest"][1])
    check("connected peer 0 code" in host_markers, "the host got id 0 and a room code")
    check("connected peer 1 code" in guest_markers, "the guest got id 1 with that code")
    check("joined peer 1" in host_markers, "the host saw the guest join")
    check("message from 1: HELLO from the guest" in host_markers, "the host received the guest's message")
    check("message from 0: WELCOME from the host" in guest_markers, "the guest received the host's message")
    check("handshake complete" in host_markers and "handshake complete" in guest_markers, "both sides finished the handshake")
    print("logs in %s" % work)
    print("PASS two games met through the relay" if failures == 0 else "FAIL %d check(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
