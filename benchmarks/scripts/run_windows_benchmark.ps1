param(
  [string]$BuildDir = "build-bench",
  [string]$Config = "Release"
)

cmake -S . -B $BuildDir -DSPEEDUP_BUILD_BENCHMARKS=ON -DSPEEDUP_ENABLE_ASM=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $BuildDir --config $Config --target speedup_benchmark_win64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $BuildDir "$Config/speedup_benchmark_win64.exe"
if (-Not (Test-Path $exe)) {
  $exe = Join-Path $BuildDir "speedup_benchmark_win64.exe"
}

if (Test-Path $exe) {
  Push-Location benchmarks/outputs
  & $exe
  Pop-Location
} else {
  Write-Error "Benchmark executable not found: $exe"
  exit 1
}
