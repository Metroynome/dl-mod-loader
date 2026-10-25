# DL Mod Loader release files

This folder holds the release module allowlist and installation notes. The
release packaging script also expects `crcs.txt` and the Horizon map-updater
scripts under `tools/horizon/`. Those inputs have not yet been supplied for
this port; use the repository's normal Make build and installation instructions.

## Install a development build

1. Enable cheats and copy `build/dist/mod-loader.<region>.pnach` into the cheat
   file for the CRC reported by PCSX2. Verified disc CRCs are NTSC-U
   **SCUS-97465 / 9BFBCD42**, PAL **SCES-53285 / D697D204**, and NTSC-J
   **SCPS-15100 / 2EC9DA96**. Use the binary files for that same region.
2. Enable host filesystem access and copy `hostfs/dl/` into the host root.
3. Edit `dl/config/modules.txt` and restart without a savestate. The secondary
   list location is `dl/modules.txt`; settings are `dl/config/*.ini`.
4. Upgrade the loader and module binaries together. Preserve your INI settings.

The release allowlist excludes development examples. Mod Menu has regional
campaign/co-op backends for NTSC-U, PAL, NTSC-J and NTSC-K. Map Loader, Forge
and multiplayer Mod Menu target NTSC-U; other multiplayer regions are not
implemented. Default modules are disabled online.

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
