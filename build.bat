@echo off
rem Builds clawd.exe with w64devkit's gcc.
setlocal
where gcc >nul 2>nul || set "PATH=%USERPROFILE%\w64devkit\bin;%PATH%"
cd /d "%~dp0"
if not exist build mkdir build

gcc -O2 -Isrc -o build\mkicon.exe tools\mkicon.c src\render.c || exit /b 1
build\mkicon.exe build\clawd.ico || exit /b 1
windres -I build res\clawd.rc -O coff -o build\res.o || exit /b 1

rem Win10 APIs (per-monitor DPI); some mingw headers default to older targets.
gcc -std=gnu11 -O2 -D_WIN32_WINNT=0x0A00 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -s -o clawd.exe ^
    src\main.c src\pet.c src\render.c src\bubble.c src\picker.c src\platform_win.c build\res.o ^
    -lgdi32 -luser32 -lshell32 -ladvapi32 -lpdh
if errorlevel 1 exit /b 1
echo built clawd.exe
