@echo off
setlocal enabledelayedexpansion
title DCR Miner - Windows Builder & Setup Assistant

echo ================================================================================
echo    ____    _____ ____    __  __ _                    
echo   |  _ \  / ____|  _ \  |  \/  (_)                   
echo   | | | || |    | |_) | | \  / |_ _ __   ___ _ __    
echo   | |_| || |____|  _ <  | |\/| | | '_ \ / _ \ '__|   
echo   |____/  \_____|_| \_\ |_|  |_|_|_| |_|\___|_|      
echo.
echo   DCRMINER v0.1.0 - Windows Automated Build & Setup Assistant
echo   Target: Decred BLAKE3 (DCP-0011) High-Performance Miner
echo ================================================================================
echo.

:: -----------------------------------------------------------------------------
:: Step 0: Check if user just wants pre-compiled binary
:: -----------------------------------------------------------------------------
if exist "dist\dcrminer-v0.1.0-windows-x64.zip" (
    echo [i] Pre-compiled standalone package is already available at:
    echo     dist\dcrminer-v0.1.0-windows-x64.zip
    echo.
    echo     If you just want to mine, you do NOT need to compile from source!
    echo     Simply extract that ZIP and run 'MINE_POOLPY.bat'.
    echo.
)

:: -----------------------------------------------------------------------------
:: Step 1: Detect Visual Studio 2022 / Build Tools C++ Environment
:: -----------------------------------------------------------------------------
echo [*] Step 1/4: Detecting Microsoft Visual C++ (MSVC) compiler...

set "VCVARS="

:: Check via vswhere if installed
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

:: Fallback to standard installation paths
if "%VCVARS%"=="" (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    )
)

if "%VCVARS%"=="" (
    echo.
    echo ================================================================================
    echo  [!] ERROR: Visual Studio C++ (MSVC) Build Tools were not found on this PC!
    echo ================================================================================
    echo.
    echo  To build dcrminer from source code, you need Visual Studio C++ Compiler.
    echo.
    echo  ------------------------------------------------------------------------------
    echo  EASIEST SOLUTION (FOR MINERS):
    echo  ------------------------------------------------------------------------------
    echo  You do NOT need to build from source!
    echo  Download the official pre-compiled standalone release directly from GitHub:
    echo  ^> https://github.com/i68i68/dcrminer/releases
    echo.
    echo  ------------------------------------------------------------------------------
    echo  DEVELOPER SOLUTION (IF YOU WANT TO BUILD FROM SOURCE):
    echo  ------------------------------------------------------------------------------
    echo  Option A: Install via Windows Package Manager (winget):
    echo            Run this command in an Administrator PowerShell:
    echo            winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    echo.
    echo  Option B: Download the installer manually from Microsoft:
    echo            https://visualstudio.microsoft.com/visual-cpp-build-tools/
    echo            (Make sure to check "Desktop development with C++" during installation)
    echo.
    echo ================================================================================
    pause
    exit /b 1
)

echo [+] Found MSVC Environment at:
echo     "%VCVARS%"
call "%VCVARS%" >nul 2>nul
echo [+] MSVC x64 Environment initialized successfully!

:: -----------------------------------------------------------------------------
:: Step 2: Check CMake
:: -----------------------------------------------------------------------------
echo.
echo [*] Step 2/4: Checking CMake build system...
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo.
    echo ================================================================================
    echo  [!] ERROR: CMake was not found in PATH!
    echo ================================================================================
    echo  Please install CMake (v3.20+) to build dcrminer.
    echo.
    echo  Install via winget:
    echo    winget install Kitware.CMake
    echo.
    echo  Or download from:
    echo    https://cmake.org/download/
    echo ================================================================================
    pause
    exit /b 1
)
for /f "tokens=*" %%v in ('cmake --version') do (
    echo [+] Detected %%v
    goto :cmake_done
)
:cmake_done

:: -----------------------------------------------------------------------------
:: Step 3: Check CUDA & Generator (Ninja vs MSBuild)
:: -----------------------------------------------------------------------------
echo.
echo [*] Step 3/4: Checking hardware toolchains (CUDA, Ninja)...

set "ENABLE_CUDA=OFF"
where nvcc >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [+] NVIDIA CUDA Toolkit (nvcc) detected! Enabling CUDA GPU acceleration.
    set "ENABLE_CUDA=ON"
) else if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin\nvcc.exe" (
    echo [+] NVIDIA CUDA Toolkit v12.6 detected! Enabling CUDA GPU acceleration.
    set "ENABLE_CUDA=ON"
    set "PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6\bin;%PATH%"
) else (
    echo [i] CUDA not detected. Building CPU (AVX-512/AVX2) + OpenCL backends.
)

set "BUILD_DIR=build_win"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

where ninja >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [+] Ninja build system detected. Using fast Ninja generator.
    set "USE_NINJA=1"
) else (
    echo [i] Ninja not found. Falling back to standard MSBuild generator.
    set "USE_NINJA=0"
)

:: -----------------------------------------------------------------------------
:: Step 4: Configure and Compile
:: -----------------------------------------------------------------------------
echo.
echo [*] Step 4/4: Compiling dcrminer (Zero-Dependency Static /MT Release)...
echo --------------------------------------------------------------------------------

if "%USE_NINJA%"=="1" (
    cmake -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=%ENABLE_CUDA% -DDCR_ENABLE_OPENCL=ON
    if %ERRORLEVEL% neq 0 (
        echo [!] CMake configuration failed!
        pause
        exit /b 1
    )
    ninja -C "%BUILD_DIR%"
    if %ERRORLEVEL% neq 0 (
        echo [!] Compilation failed!
        pause
        exit /b 1
    )
) else (
    cmake -B "%BUILD_DIR%" -A x64 -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=%ENABLE_CUDA% -DDCR_ENABLE_OPENCL=ON
    if %ERRORLEVEL% neq 0 (
        echo [!] CMake configuration failed!
        pause
        exit /b 1
    )
    cmake --build "%BUILD_DIR%" --config Release
    if %ERRORLEVEL% neq 0 (
        echo [!] Compilation failed!
        pause
        exit /b 1
    )
)

:: -----------------------------------------------------------------------------
:: Step 5: Package Executable & Quick-Start Scripts
:: -----------------------------------------------------------------------------
echo.
echo ================================================================================
echo  🎉 BUILD SUCCESSFUL!
echo ================================================================================
echo.
echo  Binaries generated:
if exist "%BUILD_DIR%\dcrminer.exe" (
    echo    ✔ %BUILD_DIR%\dcrminer.exe (Main Miner)
) else if exist "%BUILD_DIR%\Release\dcrminer.exe" (
    copy /y "%BUILD_DIR%\Release\dcrminer.exe" "%BUILD_DIR%\dcrminer.exe" >nul
    echo    ✔ %BUILD_DIR%\dcrminer.exe (Main Miner)
)

if exist "%BUILD_DIR%\dcr_benchmark.exe" (
    echo    ✔ %BUILD_DIR%\dcr_benchmark.exe (Benchmark Tool)
) else if exist "%BUILD_DIR%\Release\dcr_benchmark.exe" (
    copy /y "%BUILD_DIR%\Release\dcr_benchmark.exe" "%BUILD_DIR%\dcr_benchmark.exe" >nul
    echo    ✔ %BUILD_DIR%\dcr_benchmark.exe (Benchmark Tool)
)

echo.
echo  Quick Test Commands:
echo    - Run Benchmark:       "%BUILD_DIR%\dcr_benchmark.exe"
echo    - List Hardware:       "%BUILD_DIR%\dcrminer.exe" --list-devices
echo    - Mine on PoolPy.lol:  "%BUILD_DIR%\dcrminer.exe" -o stratum+tcp://stratum.poolpy.lol:5550 -u YOUR_WALLET.rig1 -p x
echo.
echo ================================================================================
pause
