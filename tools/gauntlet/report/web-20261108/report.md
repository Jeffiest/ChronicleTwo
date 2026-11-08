# Gauntlet report

Created 2026-11-08T10:08:08+00:00 on vm (Linux-6.18.44-fc-v310-x86_64-with-glibc2.39, 4 CPUs, 15.7 GB).
Verdict: **PASS** (exit 0).

Arguments: `--stage web-node --stage web-browser --web-build /home/user/ct-web/port/build/web --report-dir tools/gauntlet/report/web-20261108`

## Repositories

| Repository | Head | Branch | Dirty |
|---|---|---|---|
| ChronicleTwo | `58dd0868a899` | claude/dark-cloud-2-engine-yr3rlk | yes |
| DC2-Toolkit | `b5dc75ee755e` | claude/dark-cloud-2-engine-yr3rlk | yes |
| MoonMod | `875a2350f71a` | claude/dark-cloud-2-engine-yr3rlk | no |
| DC_Web | `49d71f750e25` | claude/dark-cloud-2-engine-yr3rlk | no |

## Stages

| Gate | Stage | Status | Time | Summary |
|---|---|---|---|---|
| G3 | web-node | pass | 1.8s | node darkchronicle.js exited 3 |
| G3 | web-browser | pass | 6.7s | Chromium smoke: webgpu driver exit 0, game exit 3; gl driver exit 0, game exit 3 |

### G3 web-node: pass

node darkchronicle.js exited 3

Reproduce:

```
/opt/node22/bin/node darkchronicle.js --headless --offscreen --frames 30 --save /home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-node/smoke-save
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-node/node.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-node/node.log`

### G3 web-browser: pass

Chromium smoke: webgpu driver exit 0, game exit 3; gl driver exit 0, game exit 3

Reproduce:

```
/opt/node22/bin/node /home/user/ChronicleTwo/tools/gauntlet/web/browser_smoke.cjs http://127.0.0.1:48893/index.html /smoke-data.zip /home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/browser-webgpu.log --headless --offscreen --frames 30 ; /opt/node22/bin/node /home/user/ChronicleTwo/tools/gauntlet/web/browser_smoke.cjs 'http://127.0.0.1:48893/index.html?renderer=gl' /smoke-data.zip /home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/browser-gl.log --headless --offscreen --frames 30
```

Log: `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/smoke-run-webgpu.log`

Evidence:

- `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/smoke-run-webgpu.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/browser-webgpu.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/smoke-run-gl.log`
- `/home/user/ChronicleTwo/tools/gauntlet/report/web-20261108/web-browser/browser-gl.log`
