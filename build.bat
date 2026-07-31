@echo off
rem ---------------------------------------------------------------------------
rem  LanTaskmgr build script
rem
rem  Usage:  build.bat [release|debug|clean]
rem
rem  Requires MSVC (Visual Studio Build Tools or any VS edition with the
rem  "Desktop development with C++" workload). The script locates the toolchain
rem  through vswhere and sets up the x64 environment automatically.
rem ---------------------------------------------------------------------------
setlocal EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "OUT=%ROOT%\build"
set "SRC=%ROOT%\src"
set "RES=%ROOT%\res"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=release"

if /i "%MODE%"=="clean" (
    if exist "%OUT%" rmdir /s /q "%OUT%"
    echo [build] cleaned.
    exit /b 0
)

rem --- locate MSVC --------------------------------------------------------
where cl.exe >nul 2>&1
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "!VSWHERE!" (
        echo [build] ERROR: vswhere.exe not found. Install Visual Studio Build Tools.
        exit /b 1
    )
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
    if "!VSPATH!"=="" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VSPATH=%%i"
    )
    if "!VSPATH!"=="" (
        echo [build] ERROR: no Visual Studio installation found.
        exit /b 1
    )
    call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 (
        echo [build] ERROR: failed to initialise the MSVC environment.
        exit /b 1
    )
)

if not exist "%OUT%" mkdir "%OUT%"

rem --- flags ---------------------------------------------------------------
rem /MT      static CRT  -> single self-contained exe, no redistributable
rem /O1 /Os  optimise for size (this is a background tray utility)
rem /GS      keep stack cookies: the HTTP parser is network facing
rem /Gy      COMDAT folding fodder for /OPT:ICF
set "CFLAGS=/nologo /W4 /std:c11 /MT /GS /Gy /Gw /utf-8 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I"%SRC%" /I"%RES%""
set "LDFLAGS=/nologo /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /INCREMENTAL:NO"
set "LIBS=kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib comctl32.lib ws2_32.lib iphlpapi.lib ntdll.lib psapi.lib shlwapi.lib"

if /i "%MODE%"=="debug" (
    set "CFLAGS=%CFLAGS% /Od /Zi /DLTM_DEBUG=1"
    set "LDFLAGS=%LDFLAGS% /DEBUG"
) else (
    set "CFLAGS=%CFLAGS% /O1 /Os /DNDEBUG"
)

rem --- compile resources ---------------------------------------------------
echo [build] rc  app.rc
rc.exe /nologo /I "%RES%" /I "%ROOT%" /fo "%OUT%\app.res" "%RES%\app.rc"
if errorlevel 1 exit /b 1

rem --- compile sources -----------------------------------------------------
set "SOURCES="
for %%f in ("%SRC%\*.c") do set "SOURCES=!SOURCES! "%%f""

echo [build] cl  %MODE% x64
cl %CFLAGS% /Fo"%OUT%\\" /Fd"%OUT%\LanTaskmgr.pdb" !SOURCES! /link %LDFLAGS% %LIBS% "%OUT%\app.res" /OUT:"%OUT%\LanTaskmgr.exe"
if errorlevel 1 exit /b 1

echo.
for %%f in ("%OUT%\LanTaskmgr.exe") do echo [build] OK  %%~ff  (%%~zf bytes)
endlocal
exit /b 0
