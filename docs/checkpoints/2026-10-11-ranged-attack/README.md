# Ranged attack checkpoint

Monica normal and ranged attacks are user-confirmed working. This checkpoint includes the integrated action callback, model/texture, sound lifecycle, vector storage and runtime contract fixes.

Windows game/GTest and headless completion build passed. CTest: 364 passed, five prerequisite skips, zero failed / 369. Executable SHA256: 298BAACD94B1462593494CDA5A89AF2262A048E28FE32235ED35974902FF26E0.

Remaining reported failure: the following cutscene invokes mgStoreImage while recording an active frame, which deliberately aborts. Shared effect/photo active-frame capture remains incomplete. Items/equipment/HUD and audible voice do not have individual acceptance confirmation.

Root E:/Chronicle-Two/PORT_STATE.md and HANDOFF.md remain canonical. This directory is a publication snapshot; private assets, binaries and local logs are excluded.
