# MechAssault 2: Lone Wolf — PC (unofficial)

A native Windows build of the 2004 Xbox game, made by **static recompilation**
with [xboxrecomp](https://github.com/sp00nznet/xboxrecomp): the game's own x86
program is translated function by function into C and compiled for x64, with
the toolkit replacing the Xbox kernel, GPU, audio and input. Not an emulator.

- Widescreen at up to 2.25x the Xbox's resolution, MSAA, anisotropic filtering
- Mouse and keyboard: mouse look, rebindable keys, key prompts in the HUD and
  menus (including NeuroHack), or an Xbox controller
- First-run setup: point it at your disc image and it does the rest

**You need your own copy of the game.** This repository contains no game files
and no code generated from the game. Unofficial fan project, not affiliated
with or endorsed by Microsoft or Day 1 Studios.

Made with AI assistance: Anthropic's Claude (in Claude Code) wrote most of
this project's code and its log ([MODLOG.md](MODLOG.md)), directed and
play-tested by the maintainer.

## Playing

Download the release zip, unpack it anywhere (or install it with
[Quiver Launcher](https://github.com/tgeorgiadis/quiver-launcher)), and run
`ma2.exe`. The setup window asks for your disc image (`.iso`/`.xiso`) or a
folder with `ma2.xbe`, and where to keep saves (default
`Documents\MechAssault2`). The release's `README.txt` has the controls, files,
known issues and troubleshooting.

Open bugs are in [BUGS.md](BUGS.md).

## Building

Needs Windows 10/11 x64, Visual Studio 2022 (C++ workload, MSVC), CMake 3.20+,
Python 3.10+ with `capstone`, and your MechAssault 2 disc image.

1. **Source.** Clone this repository into a folder named `ma2-recomp` (the
   steps below use that name), and xboxrecomp next to it at the commit this
   port was built on, then apply this project's runtime changes:

   ```bat
   git clone https://github.com/LegendaryXP/Mechassault2-recomp ma2-recomp
   git clone https://github.com/sp00nznet/xboxrecomp xboxrecomp
   cd xboxrecomp
   git checkout eb0aa1d
   git apply ..\ma2-recomp\patches\xboxrecomp-ma2-runtime.patch
   pip install capstone
   ```

2. **Game files.** Unpack your disc into `game\` (883 files, ~2.2 GB):

   ```bat
   cd ..\xboxrecomp
   python -m tools.xiso unpack "path\to\MechAssault 2.iso" -o ..\ma2-recomp\game
   ```

   (Or skip this step: `ma2.exe` can unpack the image itself on first run, but
   step 3 needs `game\ma2.xbe`.)

3. **Recompile.** From `..\xboxrecomp` (the disassembly takes ~15 minutes):

   ```bat
   python -m tools.xbe_parser ..\ma2-recomp\game\ma2.xbe
   python -m tools.disasm ..\ma2-recomp\game\ma2.xbe --seed-functions ..\ma2-recomp\config\seed_functions.json
   python -m tools.func_id ..\ma2-recomp\game\ma2.xbe
   python -m tools.abi_analysis ..\ma2-recomp\game\ma2.xbe
   python -m tools.recomp ..\ma2-recomp\game\ma2.xbe --all --split 250 --game-name MechAssault2 --gen-dir ..\ma2-recomp\src\recomp\gen
   ```

   `default.xbe` on the disc is only a launcher; the game is `ma2.xbe`.

4. **Build.** From this folder (~15 minutes the first time):

   ```bat
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

   The exe is `build\Release\ma2.exe`; its icon is the game's own dashboard
   image, read from `game\ma2.xbe` at build time. `play.bat` starts it from
   this folder with the release's settings, and `scripts\package.ps1` makes a
   release zip in `dist\`.

To build against a changed toolkit (including the LGPL-licensed xemu-derived
audio and GPU files), edit `..\xboxrecomp` and rebuild: `ma2.exe` relinks
against it.

## Layout

| Path | What |
|---|---|
| `src/main.c` | Entry point: setup, memory layout, kernel, then the game's entry |
| `src/ma2_setup.c` | First-run Game Path Setup window, settings, disc image unpacking |
| `src/ma2_mouse_look.c` | Mouse look for 'Mechs and vehicles |
| `src/ma2_prompts.c` | Keyboard/mouse button prompts and NeuroHack keys |
| `src/ma2_music.c`, `src/ma2_ground.c`, `src/recomp_manual.c` | Game-specific fixes and hooks |
| `config/` | Recompiler seeds (function entry points found at run time) |
| `patches/` | This port's changes to the xboxrecomp toolkit |
| `mkb.ini` | Default mouse and keyboard layout (documented inside) |
| `scripts/` | Test runs, profiling, icon and release packaging |
| `MODLOG.md` | Everything done, found and checked, day by day |

## License

This project's own code is MIT ([LICENSE](LICENSE)): `src/`, `scripts/`,
`config/`, `release/`, `CMakeLists.txt`, `mkb.ini` and the documentation.
The license does not cover, and grants nothing in:

- **MechAssault 2: Lone Wolf** itself: its program, data, art, audio and
  names, which belong to Microsoft and its licensors. Nothing of the game is
  in this repository; you need your own copy.
- **Code generated from the game's program** by the recompiler
  (`src/recomp/gen/`, not in this repository), or the icon made from the
  game at build time (`assets/`, not in this repository).
- **The xboxrecomp toolkit** and its third-party parts, which keep their own
  licenses: MIT, with LGPL-2.1-or-later parts from
  [xemu](https://github.com/xemu-project/xemu). The changes to them in
  `patches/` are under the license of the file each one changes. See
  [release/THIRD_PARTY_NOTICES.txt](release/THIRD_PARTY_NOTICES.txt).
