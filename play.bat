@echo off
rem MechAssault 2: Lone Wolf -- native PC build (xboxrecomp).
rem
rem Controls (keyboard; an Xbox/XInput controller also works):
rem   Enter = START      Backspace = BACK
rem   Z = A   X = B   C = X   V = Y     Q = White   E = Black
rem   1 = Left trigger   3 = Right trigger
rem   W A S D = left stick (move)      I J K L = right stick (aim)
rem   Arrow keys = D-pad   Shift / Ctrl = left / right stick click
rem   F11 = fullscreen on/off          close the window to quit
rem
rem Drawing runs on your graphics card (Direct3D 11); the rest of the Xbox
rem GPU (vertex shaders, command processing) is still emulated on the CPU.

cd /d "%~dp0"
if not exist "build\Release\ma2.exe" (
    echo ma2.exe is not built yet. See MODLOG.md.
    pause
    exit /b 1
)
if not exist "game\ma2.xbe" (
    echo Game files missing: extract the disc image into the "game" folder.
    pause
    exit /b 1
)
if not exist logs mkdir logs

rem Runtime switches this title needs (see MODLOG.md for why each is on).
set RECOMP_FB_WINDOW=1
set RECOMP_FB_WINDOW_EXIT=1
if "%RECOMP_WINDOW_SCALE%"=="" set RECOMP_WINDOW_SCALE=2
set RECOMP_AC97_READY=1
set RECOMP_PB_EXEC=1
set RECOMP_PB_WALK=dma
set RECOMP_ASYNC_IO=1
set RECOMP_VBLANK=1
set RECOMP_USB=1
set RECOMP_USB_PADS=1
set RECOMP_KEYBOARD=1
rem Render on the graphics card (set RECOMP_D3D11=0 for the CPU rasteriser).
if "%RECOMP_D3D11%"=="" set RECOMP_D3D11=1

start "MechAssault 2" /min cmd /c "build\Release\ma2.exe 1>logs\last_stdout.txt 2>logs\last_stderr.txt"
