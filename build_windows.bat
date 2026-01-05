@echo off
echo Building Binary Search Benchmark for Windows x64...
echo =================================================

REM Assemble with NASM for Windows x64
echo 1. Assembling binary_search_win64.asm...
nasm -f win64 -o binary_search_win64.obj bsearch.asm
if errorlevel 1 (
    echo NASM assembly failed!
    pause
    exit /b 1
)

REM Compile C code
echo 2. Compiling benchmark_win.c...
gcc -O3 -c -o benchmark_win.obj benchmark_win.c
if errorlevel 1 (
    echo C compilation failed!
    pause
    exit /b 1
)

REM Link everything
echo 3. Linking...
gcc -o benchmark.exe benchmark_win.obj binary_search_win64.obj
if errorlevel 1 (
    echo Linking failed!
    pause
    exit /b 1
)

echo.
echo Build successful! Running benchmark...
echo =====================================
echo.

benchmark.exe

echo.
echo Done!
pause