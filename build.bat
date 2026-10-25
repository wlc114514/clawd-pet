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
rem Small: Windows' own printf (not a copy of mingw's), -Os, and unused code dropped.
gcc -std=gnu11 -Os -D_WIN32_WINNT=0x0A00 -D__USE_MINGW_ANSI_STDIO=0 -Wall -Wextra -Wno-missing-field-initializers ^
    -ffunction-sections -fdata-sections -Wl,--gc-sections -municode -mwindows -s -o clawd.exe ^
    src\main.c src\pet.c src\render.c src\bubble.c src\picker.c src\platform_win.c src\wall.c src\wardrobe.c src\update.c src\icons.c build\res.o ^
    -lgdi32 -luser32 -lshell32 -ladvapi32 -lpdh -lwinhttp -lole32 -loleaut32 -lshlwapi -luuid
if errorlevel 1 exit /b 1
echo built clawd.exe
