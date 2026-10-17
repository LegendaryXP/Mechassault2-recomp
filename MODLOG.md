# MechAssault 2: Lone Wolf — native PC recomp log

## Setup
- Toolkit: sp00nznet/xboxrecomp v0.14.0 (commit eb0aa1d, 2026-10-16), cloned at ../xboxrecomp
- Project: this folder, from xboxrecomp/templates/new-game; .exe target named `ma2`
- Host: Windows 11 Home 10.0.26200, Python 3.13 (git yes; CMake, MSVC missing as of 2026-10-17)
- Game: MechAssault 2: Lone Wolf, title ID 4D53006B (MS-107). Disc not dumped yet; PC has no optical drive.

## Prior work
- No universal-modder field note for MechAssault (checked 2026-10-17).
- xboxrecomp maintainers use MechAssault 1 (XDK 4831) as a test title; toolkit fixes #133-#135 came from it.

## Log
- 2026-10-17: project scaffolded. Blocked on default.xbe (needs disc rip) and on CMake + VS2022 Build Tools.
