# Windows in-repository build repair

Validated on Windows, 2026-10-11, atop `4c331d3d`.

The reported water rewrite failure is caused by host patches being silently skipped when the generated mirror is inside the checkout. `git apply` discovers the enclosing repository and ignores paths outside its subdirectory prefix, even with exit status zero. This is [documented Git behavior](https://git-scm.com/docs/git-apply). The same script applied all patches in the existing out-of-repository local build.

The patch subprocesses now stop repository discovery at the mirror's parent and discard inherited repository-location variables. The strict two-cast water check remains unchanged. Retail sources and the patch contents are unchanged.

The fresh build also exposed runtime prerequisites: three compatibility fixtures in the build's `port` subdirectory could not find their C++ DLLs (exit `0xC0000135`). They now use the existing Windows runtime-copy hook. The workflow copies Vulkan's loader before running the completion suite, and uses `CT_TEST_TIMEOUT=60`, matching Linux/macOS CI.

Validation:

- Before repair, the original mirror script returned success inside the checkout but left water.cpp with zero of the two expected casts.
- Three registered regressions pass: all 30 patch-file changes and all Windows translation-unit rewrite counts inside/outside the checkout, inherited repository variables, and source-drift rejection before patching or creating the stamp. Entire mirror contents match across locations; retail source hashes stay unchanged. The normal game build runs these checks.
- Fresh Debug game, data-tool and GTest build passes all 498 steps in `port/build/windows-ci-mirror`, using local pinned dependency caches. Actual generated water.cpp compiles.
- Headless completion suite passes after the runtime repairs, including required-allocation failure controls, data extents, menu storage, save round trip and async lifetime checks.
- CTest: 362 passed and five skipped on the first 369-case run; two cases reached the default five-second timeout. Both pass after configuring the existing 60-second CI timeout: archive extents 8.06 s, vector rewrite 4.41 s. Result across the run and focused retries: 364 passed, five skipped, no unresolved failures. Hosted CI has not yet rerun.

Logs: `E:/Chronicle-Two/logs/windows-mirror-{regression,configure,build,completion,completion-final,ctest,ctest-retry}.log`. The complete initial build output is also in `port/build/windows-ci-mirror/build.log`.

The playtest command remains:

```powershell
& 'E:\Chronicle-Two\win\build-rel\darkchronicle.exe' --data 'E:\Chronicle-Two\win\ChronicleTwo\data' --width 1280 --height 720
```

This script/workflow repair does not alter that playtest executable. Its SHA256 remains `EB1572DFD6B93206348379E3DE049EDBC9D2432898683234B61ED8F0062CFA31`. The user's subsequent run reached the circus without a reported crash; displaced, stretched and missing character models remain unresolved.
