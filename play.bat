@echo off
rem MechAssault 2: Lone Wolf -- native PC build (xboxrecomp).
rem
rem Controls: mouse + keyboard (layout in mkb.ini -- edit it to rebind), or
rem an Xbox/XInput controller, or both at once.
rem   Mouse = look / aim            W A S D = move       Space / Shift = jump
rem   Left click = fire weapon      Right click / C = defense
rem   Wheel / Q = cycle weapons     Middle click / Z = zoom (tank)
rem   E = action    F = enter/exit vehicle    G / R = drop cargo (VTOL)
rem   1-4 = cycle cargo (VTOL)      Tab = scoreboard / tutorial text
rem   Menus: arrows move, Enter selects, Backspace goes back, Esc pauses
rem   Click the game window to use the mouse; F9 or Esc gives it back
rem   F11 or Alt+Enter = borderless fullscreen on/off
rem   close the window to quit
rem
rem Drawing and vertex shaders run on your graphics card (Direct3D 11); the
rem Xbox GPU's command processing is emulated on the CPU.

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
rem Give freed memory back. Without it every load piles on the last and
rem the 64 MB runs out (a crash after a few menu or level changes).
set RECOMP_HEAP_RECLAIM=1
set RECOMP_USB=1
set RECOMP_USB_PADS=1
rem Mouse + keyboard as a virtual controller (RECOMP_MKB=0 turns it off;
rem the older keys-only map is RECOMP_KEYBOARD=1, not both at once).
if "%RECOMP_MKB%"=="" set RECOMP_MKB=1
if "%RECOMP_MKB_INI%"=="" set RECOMP_MKB_INI=%~dp0mkb.ini
rem Render on the graphics card (set RECOMP_D3D11=0 for the CPU rasteriser).
if "%RECOMP_D3D11%"=="" set RECOMP_D3D11=1
rem Render at 2.25x the Xbox's 640x480, i.e. 1440x1080, for a 1080p screen.
rem Battle runs 60-100 fps at this size on the test PC; RECOMP_RENDER_SCALE=1
rem is the original resolution, 1.5 is 720p.
if "%RECOMP_RENDER_SCALE%"=="" set RECOMP_RENDER_SCALE=2.25
rem Start in borderless fullscreen: set RECOMP_FULLSCREEN=1 (or press F11).
rem Audio: without these DirectSound kept restarting the sound chip, and
rem three quarters of the sound came out as silence (the static).
set RECOMP_APU_SSL_IRQ=0
set RECOMP_APU_EP_MARK=1
rem The 16:9 mode is still being brought up and does not boot yet.
if "%RECOMP_WIDESCREEN%"=="" set RECOMP_WIDESCREEN=0

start "MechAssault 2" /min cmd /c "build\Release\ma2.exe 1>logs\last_stdout.txt 2>logs\last_stderr.txt"
