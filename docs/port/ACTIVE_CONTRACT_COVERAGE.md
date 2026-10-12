# Active implementation and regression coverage

`check_active_contracts.py` reads a configured build's `compile_commands.json`, generated `port_overrides.txt`, source collector, native ownership exclusions and selected game GTest sources. It reports which native translation units compiled, which superseded units leaked into the source collector or compilation database, which selected game tests compile, the source-declared GTest cases, and opt-in standalone regression targets. The deterministic JSON report is useful for review and can fail on ownership contradictions:

```powershell
python scripts/port/check_active_contracts.py --build-dir E:/Chronicle-Two/win/build-rel --check
python scripts/port/tests/test_active_contracts_batch05.py
```

The report describes the build artifacts as they exist; regenerating CMake or building targets changes the evidence. `compile_commands.json` includes ordinary targets, not necessarily every `EXCLUDE_FROM_ALL` fixture. The standalone target list comes from CMake declarations, so it does not claim those executables were built or run. `selected_gtest_sources_compiled` compares the current game-specific `TEST_SOURCES` with test translation units present in the compile database. The case names come from macros in those source files, not CTest discovery or a test run.

After upstream 1553ee88 integration on 2026-10-11, the report found 69 compiled native port translation units, 420 generated override names and 230 collector units. Among 37 superseded units there are zero production compile leaks or collector leaks. Twenty selected game GTest files declare 259 cases. CTest separately registers 356 cases: 351 pass, five prerequisite skips, zero failures. Fourteen passing UpstreamReference packet cases are isolated from the native renderer and do not establish native draw correctness. The reporter identifies those reference compile commands explicitly, and a negative control verifies the same excluded source entering a production target is still rejected. Standalone component/compatibility aggregates pass separately; see UPSTREAM_SYNC_1553ee88.md. All counts are scoped evidence, not whole-game acceptance.

`port_overrides.txt` is the generated override-name list and `port_override_sources.txt` is the configured definition-source collector. The report surfaces generated names that appear to have a `PORT_OVERRIDE` definition in a compiled native source. This is source-text matching and does not identify overloads precisely or prove a linker selected a definition. A linker map or compiler/linker symbol audit would provide stronger evidence.

The obvious-placeholder field searches compiled native source text for `PS2_UNIMPLEMENTED()` and `abort()`. This is a warning list, not behavioral classification: indirect failure helpers, empty functions, partial implementations, and stubs without those spellings are outside it. Native translation-unit compilation is not runtime reachability. Every reachability conclusion is explicitly `unknown`; the report must not be used as a whole-program safety claim.

The three negative-control fixtures verify an excluded bootstrap source remains absent from both collector and compilation database in the clean fixture, and that adding it to both is detected. They also verify selected GTest source/case extraction. They do not compile C++, validate linker resolution, or infer execution paths. The broader lexical inventory and its limitations remain documented in `COMPATIBILITY_AUDIT.md`.
