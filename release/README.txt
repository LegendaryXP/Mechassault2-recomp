MechAssault 2: Lone Wolf -- PC (unofficial), version @VERSION@
================================================================

The 2004 Xbox game, running natively on Windows: the game's own program
translated to PC code by static recompilation (xboxrecomp), not emulated.
Widescreen at high resolution, mouse and keyboard or an Xbox controller.

You need your own copy of the game. This download includes none of the
game's data files; on the first start ma2.exe asks for your disc image.

Unofficial fan project. Not affiliated with or endorsed by Microsoft or
Day 1 Studios. Made with AI assistance (Anthropic's Claude, in Claude Code).


Requirements
------------
- Windows 10 or 11, 64-bit, with a DirectX 11 graphics card.
- Your MechAssault 2: Lone Wolf disc as an image (.iso / .xiso, about 2.2 GB)
  or as extracted files (a folder with ma2.xbe in it). Tested with the
  World release (En,Ja,Fr,De,Es,It), disc version 6.
- 2.3 GB free next to ma2.exe if you give it an image (it is unpacked once).
- Windows "N" editions: install Microsoft's Media Feature Pack (some of
  the game's audio is decoded with Windows Media Foundation).


First start
-----------
1. Run ma2.exe. The "Game Path Setup" window opens.
2. Game files: press "Disc image..." and pick your .iso, or "Folder..." for
   a folder that has ma2.xbe in it.
3. Saves: where your saves go. The default is Documents\MechAssault2.
4. Continue. A disc image is unpacked into the "game" folder next to
   ma2.exe (a few seconds to a few minutes), then the game starts.

Later starts go straight to the game. To change the paths, hold Shift while
starting ma2.exe, or run "ma2.exe --setup".


Controls
--------
Xbox controller: as on the Xbox.

Mouse and keyboard (change them in mkb.ini, next to ma2.exe):
  Mouse              turn and aim            W A S D       move
  Left click         fire weapon             Space/Shift   jump (jump jets)
  Q or mouse wheel   cycle weapons           Right click/C defense
  E                  action                  F             enter/exit vehicle
  Middle click or Z  zoom (tank)             G or R        drop cargo (VTOL)
  1 2 3 4            cycle cargo (VTOL)      Tab           scoreboard/message
  T                  global/team chat        Esc or P      pause menu
  Menus: arrow keys move, Enter selects, Backspace goes back.
  NeuroHack (hijacking a 'Mech): W A S D press the buttons the HUD shows,
  left/right click the L and R triggers.
The mouse locks to the game while you play and is freed in menus and on the
pause screen. F9 frees or locks it by hand. F11 toggles fullscreen. The
game's button hints show your keys.


Files
-----
Next to ma2.exe:
  game\              your game files (when unpacked from an image)
  ma2_settings.ini   where the game files and saves are
  mkb.ini            your key bindings (made from mkb.default.ini on the
                     first start; updates never overwrite it)
  logs\              last_stderr.txt is the log to send with a bug report
  xbox_kernel.log    the runtime's own log (overwritten each start)
  shader_cache\      compiled shaders (safe to delete)
Elsewhere:
  Documents\MechAssault2        your saves (or the folder you chose)
  %LOCALAPPDATA%\xboxrecomp     the game's disk cache (safe to delete)
Saves made by earlier builds in %LOCALAPPDATA%\xboxrecomp are copied to the
new saves folder the first time; the originals stay where they were.

Uninstall: delete this folder (and the saves folder if you want).


Known issues
------------
- In widescreen, a dark copy of your 'Mech can show on water.
- Some levels' lighting is too dark in places; some shiny surfaces show odd
  pink or sky-coloured reflections.
- Menus cannot be clicked with the mouse yet (use the keys).
- The Y and Black buttons' prompts still show the controller's picture.
- Xbox Live / System Link multiplayer is not supported.


Troubleshooting
---------------
- "VCRUNTIME140.dll was not found": keep vcruntime140.dll next to ma2.exe,
  or install the Microsoft Visual C++ Redistributable (x64).
- The setup window says "No ma2.xbe in that folder": pick the folder that
  holds ma2.xbe itself, or the disc image.
- Anything else: logs\last_stderr.txt describes what happened.


Source and licenses
-------------------
Source code: @REPO_URL@ (MIT, see LICENSE.txt).
Built on xboxrecomp by sp00nz (MIT) and code from xemu (LGPL-2.1-or-later).
See THIRD_PARTY_NOTICES.txt and LGPL-2.1.txt.
