$ErrorActionPreference = 'Stop'
$repo = 'E:/Chronicle-Two/win/ChronicleTwo'
$build = 'E:/Chronicle-Two/win/build-rel'
$compiler = 'E:/Chronicle-Two/win/deps/llvm/llvm-mingw-20260922-ucrt-x86_64/bin/clang++.exe'
$options = @('-DPAL', '-DPORT', '-std=c++26', '-O2', '-ffunction-sections', '-fdata-sections', '-ffp-contract=off', '-fms-extensions', '-fno-strict-return', '-Wno-register', '-Wno-c++11-narrowing', '-Wno-return-mismatch', '-Wno-unknown-pragmas', '-Wno-writable-strings', '-include', "$repo/port/include/port.h", "-I$repo/port/include", "-I$repo/port/src", "-I$repo/tools/kazarov/include", "-I$build/ps2_include", "-I$build/ps2_include/sce")
& $compiler @options -c E:/Chronicle-Two/workers/spline-regression.cpp -o "$build/spline-regression.obj"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
function Read-FunctionText([string]$source, [string]$signature) {
    $start = $source.IndexOf("$signature {")
    if ($start -lt 0) { throw "Missing source function $signature" }
    $open = $source.IndexOf('{', $start)
    $depth = 1
    $end = $open + 1
    while ($depth -gt 0) {
        if ($source[$end] -eq '{') { $depth++ }
        if ($source[$end] -eq '}') { $depth-- }
        $end++
    }
    return $source.Substring($start, $end - $start)
}
# Compile the actual path routines without the unrelated scene-command dispatch tables.
$original = Get-Content "$repo/ps2/src/sceneseq.cpp" -Raw
$signatures = @('static void InitSplineKey(SPLINE_KEY *key)', 'C3DSpline::C3DSpline()', 'void C3DSpline::Initialize()', 'void C3DSpline::SetUpSpline(float (*points)[4], int *frames, int count, float step_speed)', 'void C3DSpline::GetNowXYZ(float *out)', 'CCameraPas::CCameraPas()', 'int CCameraPas::AddCameraPas(float *eye_point, float *look_point)', 'int CCameraPas::SetFrame(int frame_count)', 'void CCameraPas::Initialize()', 'int CCameraPas::Setup()', 'void CCameraPas::Run()', 'void CCameraPas::Step(float *eye_out, float *look_out)', 'int CCameraPas::CheckEnd()', 'CCharaPas::CCharaPas()', 'void CCharaPas::Initialize()', 'int CCharaPas::AddCharaPas(float *point)', 'int CCharaPas::Setup()', 'void CCharaPas::SetFrame(int frame_count)')
$supportText = "#include `"sceneseq.hpp`"`n#include `"mg_math.hpp`"`n#include `"vu0_ops.hpp`"`n"
foreach ($signature in $signatures) { $supportText += (Read-FunctionText $original $signature) + "`n" }
$supportText += (Read-FunctionText (Get-Content "$repo/port/src/mg_math.cpp" -Raw) 'float mgDistVector(float *a, float *b)') + "`n"
$supportText += (Read-FunctionText (Get-Content "$repo/ps2/src/mg_math.cpp" -Raw) 'void mgZeroVector(float *vector)') + "`n"
$supportText | Set-Content "$build/spline-support.cpp"
& $compiler @options -c "$build/spline-support.cpp" -o "$build/spline-support.obj"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$support = @("$build/port/CMakeFiles/dc_port.dir/__/tools/kazarov/port/sce/libvu0.cpp.obj")
& $compiler -fuse-ld=lld '-Wl,--gc-sections' "$build/spline-regression.obj" "$build/spline-support.obj" "$build/port/CMakeFiles/dc_port.dir/src/sceneseq.cpp.obj" @support -o "$build/spline-regression.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$build/spline-regression.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$supportText += (Read-FunctionText $original 'int C3DSpline::Step()') + "`n"
$supportText += (Read-FunctionText $original 'int C3DSpline::StepS()') + "`n"
$supportText | Set-Content "$build/spline-original.cpp"
& $compiler @options -c "$build/spline-original.cpp" -o "$build/spline-original.obj"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $compiler -fuse-ld=lld '-Wl,--gc-sections' "$build/spline-regression.obj" "$build/spline-original.obj" @support -o "$build/spline-baseline.exe"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$build/spline-baseline.exe"
if ($LASTEXITCODE -ne 1) { throw 'Expected the original host spline to fail the regression.' }
Write-Output 'PASS baseline reproduces the nonfinite endpoint'
exit 0
