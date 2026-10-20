# Deadlocked Mod Loader

Standalone host-file module loader for Ratchet: Deadlocked / Gladiator, adapted
from Metroynome's UYA Mod Loader. No heap edits. One loader serves campaign,
campaign co-op, the initial menu, multiplayer lobby, and splitscreen.

## Layout

```text
hostfs/dl/
  module.txt
  mods/
    hello.ntscu.bin
    hello.pal.bin
    hello.ntscj.bin
    hello.ntsck.bin
    campaign-test.<region>.bin
    multiplayer-test.ntscu.bin
    debug/exceptionhandler.bin
modules/
  hello/
  campaign-test/
  multiplayer-test/
release/
  mod-loader.<region>.pnach
  hostfs/dl/...
```

Edit `hostfs/dl/module.txt`, outside `mods/`. Build output is also packaged in
`release/hostfs/dl/`. Source modules each have their own directory and JSON
manifest. Generated binaries, build output, and local tests are gitignored.

## Enable modules

```text
// 0 = disabled, 1 = campaign/co-op, 2 = multiplayer, 3 = both
hello 3
campaign-test 1
multiplayer-test 2
// Explicit regional selection is also supported:
campaign-test.pal 0
```

Names accept `name`, `name.bin`, `name.ntscu`, or `name.ntscu.bin` (also `pal`,
`ntscj`, `ntsck`). Automatic names select the running region. Explicit entries
for another region are skipped before file access. Binaries live directly in
`mods/`, without region subdirectories. Legacy `modules/` prefixes are accepted.
Comments can use trailing `//` or whole-line `#`; CRLF and LF are supported.
Disabled entries never open files. Duplicate names resolve to one canonical
filename; the first successfully loaded entry wins, including its mode mask.

Restart emulation after changing the list or module binaries. The list loads
once; a missing list is retried every 300 callbacks. Missing/invalid modules are
logged and skipped. Rebuild and deploy all module binaries together because
adding or resizing one module can change the others' linked addresses.

## Create modules

```sh
python tools/build.py new example
python tools/build.py new campaign-example --library librac4
python tools/build.py new multiplayer-example --library libdl
python tools/build.py new paired-example --library both
# Equivalent make command:
make new-module NAME=example LIBRARY=none
```

New entries are disabled in `module.txt`. `none` uses only the mode-aware
`ModuleApi` and supports both modes. `librac4` modules run in campaign/co-op;
`libdl` modules run in multiplayer. `both` creates separate campaign and
multiplayer source files and binaries in one source directory, preventing
collisions between the libraries' shared symbol names.

`moduleInit(const ModuleApi *api)` runs once when its mode first becomes active.
A negative return disables that module's update callback. `moduleUpdate(void)`
runs on matching hook calls, including the initialization call. Retain the API
pointer, not a copy; its mode is refreshed during transitions. Check that game
objects and overlays are ready before using library functions, and do not retain
player pointers across level changes. A loaded module stays allocated for the
rest of the boot. There is no hot unloading.

The `hello` module prints every 300 callbacks, as in SM/UYA. These are hook calls,
not guaranteed video frames. Library-backed examples use `printf`; library-free
modules use `api->print`. Output goes to the emulator console, not the game HUD.

## Build and regions

Python 3.8+, make, and the PS2 EE compiler/PS2SDK are required. `EE_PREFIX` defaults
to `ee-`. Libraries are copied into the build directory and built there; their
source checkouts are not modified.

- `LIBRAC4_DIR`: defaults to sibling `../librac4`. Supports NTSC-U, PAL, Japan,
  and Korea using its regional Makefiles.
- `LIBDL_DIR`: defaults to `../../Horizon/libdl`. The available Horizon library
  contains NTSC-U addresses only. Multiplayer library scaffolds therefore target
  NTSC-U; attempts to link it for other regions fail rather than guess addresses.
- The loader and library-free modules build for all four regions. Non-US
  multiplayer library support requires a separately verified libdl port.

```sh
python tools/build.py check
python tools/build.py build
python tools/build.py build --region ntscu
# or: make REGION=all
```

## Docker (no local library checkouts needed)

From the repository directory, build the toolchain image once:

```powershell
docker build -t dl-mod-loader .
docker run --rm -v "${PWD}:/workspace" dl-mod-loader
```

The Dockerfile runs `docker-init.sh` to install Python/make and clone pinned
revisions of `Metroynome/librac4` and `Dnawrkshp/libdl` into the image. The
container's default command builds both the loader cheat and all configured
modules for all four regions. Outputs remain in this repository's `build/`,
`hostfs/dl/mods/`, and `release/` folders. No sibling library mounts are needed.
Network access is needed when building the image; normal module builds use the
cloned sources already in the image.

```powershell
# Build just NTSC-U:
docker run --rm -v "${PWD}:/workspace" dl-mod-loader make REGION=ntscu
# Create a module, then enable its entry in hostfs/dl/module.txt and rebuild:
docker run --rm -v "${PWD}:/workspace" dl-mod-loader make new-module NAME=my-mod LIBRARY=librac4
```

For intentional library upgrades, pass `--build-arg LIBRAC4_REF=<commit>` and/or
`--build-arg LIBDL_REF=<commit>` to `docker build`. Library region restrictions
still apply. The build copies library sources into `build/` before compiling.

`docker-init.sh` can also initialize a fresh base-image container and run a
command with the dependency paths exported:

```powershell
docker run --rm -v "${PWD}:/workspace" -w /workspace --entrypoint sh dnawrkshp/ps2dev-libdl:latest ./docker-init.sh make
```

Running `sh docker-init.sh` alone prepares dependencies without building. It
refuses to reset existing modified or mismatched checkouts. Use it inside the
Linux container, not directly in Windows PowerShell.

## Make workflows inside the container

After `sh docker-init.sh`, you can run these from the repository root:

```sh
make clean && make             # loader cheat and every configured module
make modules                   # same complete build, including loader
make module NAME=hello         # rebuild one source module folder
make module NAME=hello REGION=ntscu
cd modules/hello
make clean && make             # only this module, all its supported regions
make REGION=ntscu
```

`make clean` at the root removes generated build/release directories and regional
hostfs binaries. It keeps module sources, `module.txt`, and the optional debug
payload. A module-folder clean removes only that module's objects/binaries and
keeps its address reservation. Generated module folders include a Makefile.

Single-module builds use the existing regional layout. If there is no layout,
the first invocation builds all modules to establish one. If a module grows
beyond its reserved size or the module set/library/mode configuration changes,
run a full root build and deploy all binaries together. A `both` source folder
rebuilds both of its library variants. Restart emulation to load new binaries.
A fresh project still needs the root build to produce the loader PNACH itself.

To work interactively using the Dockerfile image:

```powershell
docker run --rm -it -v "${PWD}:/workspace" dl-mod-loader sh
```

The image is already initialized. The dependency directories are also discovered
automatically by the build tool, so running `sh docker-init.sh` in a base-image
shell does not require sourcing the script before subsequent `make` commands.

## Use in PCSX2

1. Use `release/mod-loader.<region>.pnach` as the cheat contents for your game's
   CRC and enable cheats. Do not combine it with Horizon or other patches using
   the same hook or low-memory ranges.
2. Enable host filesystem access. Arrange the host root so `host0:dl/module.txt`
   resolves to the packaged list. PCSX2 commonly uses the ISO's directory as its
   host root: with an ISO in `D:/PS2/ISOs`, copy packaged `dl/` into that directory.
3. Cold-boot the game. The initial menu runs campaign/both-mode modules.

The optional `mods/debug/exceptionhandler.bin` is the same validated payload
used by the UYA/SM loader. Remove or rename it to disable it and cold-boot. Its
absence is harmless. The loader checks size, hash, and memory occupancy before
installing it once. Keep the source payload absent when rebuilding if you want
it omitted from the release package too.

## Runtime and validation

Campaign uses the cheat-template return hook at `0x00139a48`; multiplayer uses
Horizon's `sceGsSyncV` return hook at `0x00138dfc`. Runtime mode filtering prevents
duplicate callbacks. Both original stack-pop delay slots remain intact; the entry wrapper preserves EE return registers and `gp`.
SDK instruction signatures are checked before file calls. Regional level globals
select campaign/co-op versus multiplayer/splitscreen; unknown IDs defer updates.
See [ADDRESSES.md](ADDRESSES.md) for sources and verification limits.

| Range (end exclusive) | Use |
|---|---|
| `0x000c8000?0x000ce000` | Optional exception handler |
| `0x000f0000?0x000f2000` | Loader |
| `0x000f2000?0x000fe000` | Modules, including BSS (48 KiB) |
| `0x000fe000?0x00100000` | Persistent state |

At most eight modules are loaded. Header checks cover the DL-specific ABI magic,
region, mode mask, entry offsets, sizes, bounds, and overlap. Reads detect short
or oversized files; BSS is cleared and caches synchronized before execution.
The DL magic prevents accidentally accepting UYA module binaries.

```sh
python tools/verify_addresses.py 'D:/PS2/ISOs/RAC Levels/rac4'
```

`make test` runs the local parser/runtime/platform harness; `tests/` remains
ignored, matching the UYA repository convention. Hardware behavior and full
campaign/multiplayer transitions still require runtime testing for each disc.

Verified locally: four-region builds and boot-core signature checks; parser,
runtime, and regional mode tests; integration linking real librac4/libdl
functions; NTSC-U PCSX2 cold boot with campaign/hello initialization, inactive
multiplayer module at the menu, and a continuously advancing hello counter.
Other regions and full campaign/multiplayer transitions are not live-tested yet.
