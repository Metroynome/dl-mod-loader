# Deadlocked address evidence

The four retail `rac4.boot.<region>.v100.retail.core.0001.bin` files share the
resident SDK at base `0x0011a180`. Ghidra's `DL_Retail_BootELF_974.65.elf`
identifies sceOpen, sceClose, sceRead, FlushCache, and sceGsSyncV by name.
The FlushCache syscall sequence anchors the extracted core mapping.

| Function | Address | First word |
|---|---|---|
| printf (both libraries) | 0x0011d5d8 | 0x27bdff70 |
| sceOpen | 0x0012b338 | 0x27bdfeb0 |
| sceClose | 0x0012b5c8 | 0x27bdff60 |
| sceRead | 0x0012b980 | 0x27bdff20 |
| FlushCache | 0x00126e20 | 0x24030064 |

The loader wraps the sync entries at `0x001399e0` (campaign) and
`0x00138d70` (multiplayer). The displaced prologues are `addiu sp,sp,-64` /
`sd s1,16(sp)` and `addiu sp,sp,-32` / `sd s0,0(sp)`, respectively.
Each trampoline replays that prologue, runs the original function, then updates
modules. This preserves drawing timing while leaving Horizon's return hook at
`0x00138dfc` intact. PNACH templates check nearby original instructions before
installing either entry wrapper.

The five SDK signatures above and both entry prologues were compared against
all four extracted retail cores. No regional entry addresses were inferred.
Runtime mode filtering selects one wrapper; unknown level IDs defer updates.

| Region | Current level global (librac4/include/game.h) |
|---|---|
| NTSC-U | 0x0021de10 |
| PAL | 0x0021de90 |
| NTSC-J / NTSC-K | 0x00238610 |

Level IDs come from `RAC Levels/rac-defs.json`, game 4: boot menu -1;
campaign planets 1 through 15 (sparse), co-op +20, multiplayer lobby 39,
multiplayer planets +40 and splitscreen +60. Level 0 is the multiplayer
front-end/staging state. The adapter enumerates known IDs and checks native
menu/connection state before assigning Local Play or online contexts.

The available libdl is NTSC-U only. Loader/library-free builds cover all four
regions; builds and matching core signatures do not establish complete runtime
support. NTSC-U Local Play tests cover menu/input, map scanning, custom/retail
starts and hops, reloads, mode presets, HUD, minimaps and loading backgrounds.
Other multiplayer regions and campaign menu rendering still need porting.

## Forge mode presets (NTSC-U)

The live resident `nwSetGameSetupFlagsForGameType(int)` routine is at
`0x001608d0`, beginning with `0x27bdfff0`. Its call at `0x001608dc` targets
`0x00160870`, which copies the 88-byte default setup at `0x00173840` to
`0x001737e8`. The following switch handles native modes 0 through 4 and applies
their objective/spawn flags. Its CQ and CTF cases match the decompiled routine
in `deadlocked-proto-decomp/game_dl/network/nwObjects.cpp`.

Map Loader uses this preset when a mailbox request changes the mode, transfers
its game flags into libdl's active `GameOptions`, and synchronizes the resident
setup before loading. These addresses have not been verified for other regions.

NTSC-U Local Play staging's active object is at `*(u32*)0x011c70c8`, matching
`*(u32*)0x011c7108` when that screen is active. Its map-name and game-mode child
pointers are at `0xb0` and `0xb4`; each owns inline text at `0x60`. Live staging
reads identified these as the current retail map name and "Conquest" respectively.
