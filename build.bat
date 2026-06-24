@echo off
chcp 65001 >nul 2>&1
cd /d "%~dp0"

echo.
echo   ============================================================
echo     DX11 Multi-Window Sync v3.0 - Build
echo   ============================================================
echo.

rem --- Find Visual Studio ---
set "VCVARS="

if exist "%ProgramFiles%\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2026\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if "%VCVARS%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\2026\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2026\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
if "%VCVARS%"=="" if exist "%ProgramFiles%\Microsoft Visual Studio\2026\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2026\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)
if "%VCVARS%"=="" if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2026\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

if "%VCVARS%"=="" (
    echo   [ERROR] Visual Studio 2026 not found!
    echo.
    echo   Install steps:
    echo     1. Download VS 2026 Community from https://visualstudio.microsoft.com/
    echo     2. During install, check "Desktop development with C++"
    echo     3. Run build.bat again after install completes
    echo.
    pause
    exit /b 1
)

echo   [1/3] Initializing MSVC toolchain...
call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
    echo   [ERROR] vcvars64.bat failed!
    echo   Try opening "Developer Command Prompt for VS 2026" from Start Menu
    echo   then cd to this folder and run build.bat again.
    pause
    exit /b 1
)
echo         Toolchain ready.

echo.
echo   [2/3] Compiling (x64 Release, static CRT, zero deps)...

set "SRC=src\main.cpp src\app_window.cpp src\sync_engine.cpp src\window_manager.cpp src\key_blacklist.cpp resource.rc"
set "OUT=dx11_sync.exe"
set "LIBS=user32.lib gdi32.lib dwmapi.lib d2d1.lib dwrite.lib comctl32.lib"
set "OPTS=/nologo /EHsc /O2 /MT /W3 /utf-8"
set "DEFS=/D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A00000B /DWIN32_LEAN_AND_MEAN"

cl %OPTS% %DEFS% /Fe:%OUT% %SRC% /link %LIBS% /MANIFEST:NO /SUBSYSTEM:WINDOWS

if errorlevel 1 (
    echo.
    echo   ============================================================
    echo   [BUILD FAILED] See error details above.
    echo   ============================================================
    pause
    exit /b 1
)

echo.
echo   ============================================================
if exist %OUT% (
    echo     BUILD SUCCESS
    echo.
    echo     Output: dx11_sync.exe
    echo.
    echo     Double-click dx11_sync.exe to run.
    echo     No DLLs or runtime install needed.
) else (
    echo     Output file not found!
)
echo   ============================================================
echo.
pause
