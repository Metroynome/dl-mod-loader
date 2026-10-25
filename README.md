# DL Mod Loader

A host-filesystem mod loader for Ratchet: Deadlocked / Gladiator. The loader
supports campaign/co-op and multiplayer without changing the game's heap.
Modules use librac4 for campaign or libdl for multiplayer; Mod Menu is optional.

## Build

From the repository root:

```powershell
docker build -t dl-mod-loader .
docker run --rm -it -v "${PWD}:/workspace" dl-mod-loader sh
```

The Dockerfile runs `docker-init.sh`, installs the build dependencies and
`bin2code`, and clones pinned librac4/libdl sources. Inside the container:

```sh
make                          # loader and modules for their supported regions
make REGION=ntscu              # NTSC-U only
make NAME=forge REGION=ntscu   # one module folder
cd modules/forge
make clean && make             # build that module independently
```

To use an existing PS2DEV container instead, mount this repository, enter its
mounted directory, and run `sh docker-init.sh` before `make clean && make`.
The initialization script prepares dependencies; it does not build unless you
pass a command. Library sources are compiled in isolated copies under `build/`.

Outputs:

- `build/dist/mod-loader.<region>.pnach`: loader cheat.
- `hostfs/dl/mods/<name>.<region>.bin`: module binaries.
- `hostfs/dl/mods/utils/mapscan.irx`: custom-map directory helper.

`make clean` removes generated builds and regional module binaries. It preserves
source files, the module list, INI settings and the optional exception handler.
No Python is required. The C relocation tool packages module binaries; the
loader chooses their runtime addresses and performs relocation.

## Install in PCSX2

1. Enable cheats and copy the loader PNACH contents into the cheat file matching
   the CRC reported by PCSX2. The tested NTSC-U Deadlocked CRC is `9BFBCD42`.
2. Enable host filesystem access and copy `hostfs/dl/` into the emulator's host
   root. With an ISO in `D:/PS2/ISOs`, that is normally `D:/PS2/ISOs/dl/`.
3. Edit `dl/config/modules.txt`, then fully restart without a savestate.

The list falls back to `dl/modules.txt` if `dl/config/modules.txt` is absent.
Settings live in `dl/config/<mod>.<region>.ini`.

```text
modmenu local
maploader local
forge local
bios-warning local
hello disabled
```

Contexts are `campaign`, `local`, `online`, or comma-separated combinations.
`disabled` prevents loading. Legacy numeric modes remain accepted: 0 disabled,
1 campaign, 2 multiplayer, 3 both. A module may further restrict its contexts.
Default gameplay modules stay inactive online so Horizon can take over.

Entries may omit `.bin` and the region: `forge` selects the current region;
`forge.ntscu` selects that exact binary. Files stay directly in `dl/mods/`.
Use whole-line `#` comments or trailing `//` comments. Duplicate entries load
only once. The loader logs missing/invalid modules and skips them.

Restart after changing module binaries or the list. Module placement is dynamic;
modules do not need a fixed address in their Makefiles.

## Modules

| Module | Purpose |
| --- | --- |
| modmenu | Shared menu and tab API; optional for other modules |
| maploader | Local Play retail/custom-map selection and refresh |
| forge | Reloads, map hopping, game-mode choices and PINE commands |
| bios-warning | Optional warning for incompatible expanded-RAM BIOS settings |
| menu-example | Example tab and configuration persistence |
| hello / campaign-test / multiplayer-test | Development examples |

The loader and library-free modules build for NTSC-U, PAL, NTSC-J and NTSC-K.
librac4 has regional campaign builds. The available libdl and the current
Mod Menu, Map Loader and Forge implementations target **NTSC-U multiplayer**.
Campaign menu rendering and other multiplayer regions still need porting.
A successful regional build does not verify its runtime addresses.

Open Mod Menu with L3 + R3 in Local Play menus. During gameplay, use MOD MENU
from the pause menu. Forge's reload shortcut is **L1 + D-pad Up + Circle**.
See [Forge](modules/forge/README.md) and its [PINE interface](modules/forge/PINE.md)
for game-mode, configuration and editor-integration behavior.

Custom maps belong directly in `dl/`, alongside `mods/` and `config/`. Keep the
map pack's matching `.version`, `.wad`, `.world`, `.sound`, `.map` and `.bg`
files together. Map Loader scans on the first Local Play staging visit and
retains its catalog in memory. Square on Custom Maps refreshes it; Forge and
PINE can refresh it too. No index files or external indexing scripts are needed.
Maps requiring Horizon-specific custom code or game modes are unsupported.

## Create a module

```sh
make new-module NAME=my-mod LIBRARY=none
make new-module NAME=my-campaign-mod LIBRARY=librac4
make new-module NAME=my-multiplayer-mod LIBRARY=libdl
make new-module NAME=my-paired-mod LIBRARY=both
```

Each folder contains a Makefile, `Makefile.defs` and C sources. The scaffold adds
a disabled entry to the module list. Edit that entry to enable the module.
`both` creates separate campaign and multiplayer binaries to avoid collisions
between the libraries' shared symbols.

`moduleInit(const moduleApi_t* api)` runs once; returning negative disables
updates. `moduleUpdate(void)` runs on loader hook callbacks, which are not
necessarily video frames. Keep the API pointer, check object/overlay readiness,
and release hooks/input/UI through the lifecycle callbacks before deactivation.
Modules can use services and INI persistence without depending on Mod Menu.
Public interfaces live in [include/module.h](include/module.h) and each module's
`include/` folder. The shared menu supports named left/right range choices via
`setItemValueLabel`.

## Memory and debugging

The loader stays at `0x000a0000`. Modules follow its aligned end up to
`0x00100000`, or use `0x02000000` onward when 128 MiB EE RAM and a compatible
BIOS are detected. Discovery pointers and the loaded-module table stay in low
memory. See [address evidence](ADDRESSES.md) and the PINE discovery instructions.

The optional exception handler loads from `dl/mods/debug/exceptionhandler.bin`
at `0x00098000`, reserving `0x6000` bytes. Remove that file to disable it and
restart. Do not combine patches that overwrite the loader or module memory.

The existing GitHub workflow builds release ZIPs from `release/` policy files.
Release packaging still needs verified DL regional CRC policy and the map-updater
assets expected by `tools/release.sh`; those inputs are not yet present in this
port. Development builds and installation above do not depend on them.
