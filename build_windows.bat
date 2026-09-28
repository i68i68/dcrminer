@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo   Building DCR BLAKE3 Miner for Windows (x64)
echo ===================================================

:: Look for Visual Studio vcvars64.bat
set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

if not "%VCVARS%"=="" (
    echo Initializing MSVC x64 environment...
    call "%VCVARS%"
)

set "BUILD_DIR=build_win"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where nvcc >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo NVIDIA CUDA Compiler (nvcc) detected. Enabling CUDA.
    set "ENABLE_CUDA=ON"
) else (
    echo nvcc not found in PATH. Building CPU/OpenCL backends.
    set "ENABLE_CUDA=OFF"
)

where ninja >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo Using Ninja generator...
    cmake -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=%ENABLE_CUDA% -DDCR_ENABLE_OPENCL=ON
    ninja -C "%BUILD_DIR%"
) else (
    echo Using Visual Studio generator...
    cmake -B "%BUILD_DIR%" -A x64 -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=%ENABLE_CUDA% -DDCR_ENABLE_OPENCL=ON
    cmake --build "%BUILD_DIR%" --config Release
)

echo.
echo ===================================================
echo Build complete! Executable is at %BUILD_DIR%\dcrminer.exe
echo ===================================================
