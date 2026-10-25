# DL Mod Loader release files

This folder holds the release module allowlist and installation notes. The
release packaging script also expects `crcs.txt` and the Horizon map-updater
scripts under `tools/horizon/`. Those inputs have not yet been supplied for
this port; use the repository's normal Make build and installation instructions.

## Install a development build

1. Enable cheats and copy `build/dist/mod-loader.ntscu.pnach` into the cheat file
   for the CRC reported by PCSX2. The tested NTSC-U Deadlocked disc uses
   **SCUS-97465 / 9BFBCD42**. Other disc CRCs remain unverified here.
2. Enable host filesystem access and copy `hostfs/dl/` into the host root.
3. Edit `dl/config/modules.txt` and restart without a savestate. The secondary
   list location is `dl/modules.txt`; settings are `dl/config/*.ini`.
4. Upgrade the loader and module binaries together. Preserve your INI settings.

The release allowlist excludes development examples. Current gameplay/menu
modules use NTSC-U libdl; other multiplayer regions and campaign menu rendering
are not implemented. Default modules are disabled online.

Custom-map packs go directly in `dl/`. Map Loader scans on the first Local Play
staging visit and retains its list across games. Press Square on Custom Maps,
use Forge's Refresh Custom Maps, or send a PINE refresh after changing files.
There are no `.idx` files. `dl/mods/utils/mapscan.irx` supplies directory access.
Maps requiring Horizon-specific custom code/game modes are unsupported.

The optional exception handler belongs at `dl/mods/debug/exceptionhandler.bin`.
Remove it and restart to disable it. Expanded-RAM support requires a compatible
BIOS; dismissing the BIOS warning does not change emulator settings.

See the repository README and `modules/forge/PINE.md` for build instructions,
menu controls, limitations and editor integration.
