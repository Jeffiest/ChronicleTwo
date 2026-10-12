# Opening capture and clean-build repair

Validated on Windows, 2026-10-11, atop upstream merge `25388b4e`.

## Evidence and changes

The user's workshop run ends with `mgStoreImage: synchronous color capture '' during recording/frame is unsupported`. The screenshots separately show missing/stretched character geometry; the crash output does not establish a model-loading failure. The user skipped part of the Monica cutscene, so complete playback remains unaccepted.

Backbuffer capture now reads the completed canonical frame while temporarily suspending the unfinished display list. It restores the same list, pending entries, target and interpolation state. It does not render or present the unfinished tick. This serves the shared fade and screen-effect callers. Current-target/explicit-texture reads during recording and all active GPU-frame reads remain unsupported, including photo paths that need pending draws.

The pasted Linux failure runs `ct_active_contract_check` before `port_overrides.txt` exists. Its CMake target now explicitly depends on that generated output. This applies to clean builds on every platform; the compiler warning about extraction timestamps is unrelated to the failure.

## Validation

- Actual `CFadeInOut::CaptureScreen` runs three times during recording, reads the prior pattern, preserves list identity/entries/serial/target/interpolation and respects destination canaries. Pending draws still execute afterward. Invalid bounds, current targets, explicit textures and active GPU frames reject without changing the output. Existing packing, orientation, resize and actual menu capture checks pass.
- A fresh Windows Debug build directory builds only `ct_active_contract_check`; Ninja generates the override list first and the ownership check/three negative controls pass. Pinned dependencies were reused from local source/vendor caches after the initial restricted-network download failed. No existing generated override file was copied.
- Weighted production-model test now includes the workshop `p1_starbre` pack: 56 actual weighted MDTs survive scratch reuse and eight reloads, with three bind-pose skinning passes and a translated copied skeleton. This does not test cutscene motion playback, visibility or final rendering and does not establish a fix for the screenshots.
- Game/GTest/headless completion build passes. CTest: 364 passed, five skipped, zero failed / 369. Full Linux/macOS builds and hosted CI have not been run locally.
- Executable SHA256: `EB1572DFD6B93206348379E3DE049EDBC9D2432898683234B61ED8F0062CFA31`.

Logs under `E:/Chronicle-Two/logs`: `opening-capture-{build,test}.log`, `clean-order-{configure,build}.log`, `opening-model-{build,test}.log`, `opening-final-{build,ctest}.log`.

Character deformation remains unresolved. Next coverage needs the real motion/character setup and simultaneous live copies through scene transitions, beyond loading and static bind-pose fixtures. User gameplay acceptance of the capture repair is pending. Retail sources and Kazarov submodule are untouched. Root PORT_STATE.md records publication status.
