# Upstream master compatibility merge

Fresh upstream master: `1553ee88664bb4e7418a8882bcc123a3e048ca87`.
Native checkpoint: `6d2edf3605bfb0fe38c345f446fa83ee61327c5b`.
Requested publication branch: `Jeffiest/ChronicleTwo:port/upstream-first-2026-10-11`.

The seventeen upstream commits after `4ee90030` were already adapted in `568d125a`, as recorded in `UPSTREAM_SYNC_1553ee88.md`. That checkpoint did not record upstream as a merge parent. This standard merge records that ancestry and preserves the validated adaptations. It is not seventeen new gameplay fixes.

## Conflict decisions

| Files | Resolution and reason |
|---|---|
| `port/CMakeLists.txt` | Retain upstream build/data/long64 setup plus native ownership, checked host rewrites and runtime test registration. |
| `port/include/stubs/mainloop.hpp`, `port/src/mainloop.cpp` | Retain the complete native loop, PAL controller bindings and shared scheduler adaptation. The upstream alias wrappers target a different retained source owner. |
| `port/include/stubs/mglib.hpp`, `port/src/mglib.cpp` | Retain upstream frame timing plus native renderer and completed-frame capture integration. Replacing it with the bootstrap version would remove working rendering. |
| `port/src/dataread.cpp`, `port/src/tests/dataread_test.cpp` | Retain DATA-only indexing/host0/sector padding plus bounded archive diagnostics and native asynchronous publication. |
| `port/src/mg_memory.cpp` | Retain host-width headers and peak tracking with the tested native bounds and arena lifecycle. |
| `port/src/main.cpp` | Retain the working native driver, audio, crash diagnostics and multi-hook config wiring; upstream shader pipeline and scheduler setup are already adapted. |
| `port/src/platform/config.cpp`, `config.hpp`, `port/src/tests/platform_config_test.cpp` | Retain upstream unreadable-file protection/change notification through the expanded native configuration API and its tests. |
| `port/src/tests/CMakeLists.txt`, `port/src/tests/cmake/data.cmake` | Retain upstream data/prog0/fiber/math tests, native runtime fixtures and isolated packet references. The upstream smoke test expects boot to abort at the first hardware call and is obsolete for this running port. |
| `scripts/port/header_fixups.py` | Retain upstream header/linkage corrections plus the native serialized function-record width and template fixes. |

Accept upstream deletion of `port/src/platform/settings.cpp`: this fallback was already explicitly excluded by NativeOwnership.cmake; config.cpp owns SettingsGet/SettingsChange. Retain the two empty .gitkeep placeholders added by upstream. No authored retail or submodule changes; original ChronicleTwo/CT_* naming remains.

## Validation

Game, GTest and headless completion targets build successfully. CTest: **364 passed, five skipped, zero failed / 369**. The skips require TTF glyph and desktop mouse-capture prerequisites. Compatibility inventory remains at 5,508 findings. Retail sources, docs/PC.md and Kazarov pin match upstream exactly; no unresolved conflicts or diff whitespace errors remain.

Logs: `E:/Chronicle-Two/logs/upstream-ancestry-build.log` and `E:/Chronicle-Two/logs/upstream-ancestry-ctest.log`.
Executable SHA256: `1955A6A128315A6B46FCBE0A99189BA954263528621DE08C22C4A9E7DB2A1E0D`.
Merge commit and remote verification are pending at this receipt's creation; root PORT_STATE.md records final publication.

User-confirmed normal/ranged attacks belong to the native checkpoint. The later cutscene still reaches the deliberate recording/frame capture guard; no repair of that gap is claimed by this merge. No new gameplay run, PS2 byte-match check or other-platform build was performed.
