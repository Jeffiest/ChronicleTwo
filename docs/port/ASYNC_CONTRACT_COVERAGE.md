# Native async contract coverage

`port/src/tests/async_contract_standalone.cpp` is a small integration fixture intended to link
against the production native port. It builds a temporary DATA.HD4/DATA.DAT tree and calls the
real `InitCDFile`, `LoadFileBG`, `GetReadBGFile`, `ReadBGSync`, and `BreakReadBG` entry points.
It covers missing lookup, a short physical extent, allocation/busy state, the two-poll completion
contract, destination bytes, and cancellation. The malformed short extent is generated in the
fixture itself; this makes a `cannot read ...DATA.DAT` diagnostic expected evidence of a rejected
truncated read in this case, but does not explain any unrelated runtime trace.

Current native behavior is synchronous at the filesystem boundary: `LoadFileBG` reads bytes
before returning and queues metadata afterward. `bg_pending` then emulates one pending poll, so
the first `ReadBGSync` returns busy and the second reports completion. `BreakReadBG` polls once,
clears all queue slots when any entry remains pending, and preserves completed entries when none
remain. This is a host compatibility contract, not evidence of parallel I/O. In particular, the
fixture's cancellation checks queue bookkeeping and buffer already populated by the production
call; it does not claim to interrupt an OS read.

`eekernel.cpp` implements cooperative scheduling at explicit yield points, with a single emulated
EE thread running at a time. The inspected calls include ready-queue rotation, semaphore wait and
signal, and termination. No new thread or semaphore fixture is included here: without pinned PAL
observations for priority handoff, semaphore deletion, and termination timing, a synthetic test
would only restate the host implementation. These edge semantics remain unverified.

Integration wires `ct_async_contract_test` into both completion aggregates and CI. The first linked
run exposed successful-size publication before a failed physical read. `LoadFileBG` now reads before
publishing metadata, leaving output size zero and no busy entry on failure. The fixture reserves a
full 2,048-byte disc sector plus a canary, verifies zero padding and checks the canary remains intact.
The production-linked fixture passes; the truncated-read diagnostic is expected negative-control output.
