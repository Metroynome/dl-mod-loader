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

After an NTSC-U Horizon logout, the patch can leave its map callbacks installed.
Map Loader recognizes the first 64 callback bytes using the hashes in
`horizonHooks` and replaces those calls only in confirmed Local Play. It also
restores the loading-screen return, mode-name copy, and map-initialization call
when their known Horizon callbacks match. Retail return instructions at
`0x00706c74` (menus) and `0x0062e524` (game) were checked in extracted code5.
Unknown patch builds are left intact. Online execution never reclaims these hooks.

The five SDK signatures above and both entry prologues were compared against
all four extracted retail cores. No regional entry addresses were inferred.
Runtime mode filtering selects one wrapper; unknown level IDs defer updates.

| Region | Current level global (librac4/include/game.h) |
|---|---|
| NTSC-U | 0x0021de10 |
| PAL | 0x0021de98 |
| NTSC-J / NTSC-K | 0x00238618 |

Level IDs come from `RAC Levels/rac-defs.json`, game 4: boot menu -1;
campaign planets 1 through 15 (sparse), co-op +20, multiplayer lobby 39,
multiplayer planets +40 and splitscreen +60. Level 0 is the multiplayer
front-end/staging state. The adapter enumerates known IDs and checks native
menu/connection state before assigning Local Play or online contexts.

The available libdl is NTSC-U only. Loader/library-free builds cover all four
regions; builds and matching core signatures do not establish complete runtime
support. NTSC-U Local Play tests cover menu/input, map scanning, custom/retail
starts and hops, reloads, mode presets, HUD, minimaps and loading backgrounds.
Other multiplayer regions still need porting.

## Campaign Mod Menu

librac4's `vaDrawTextFunc`, `vaGetTextWidthFunc` and `vaDrawBoxFunc` tables
resolve the native renderer per region and planet. Mod Menu's `campaign.h`
uses `VariableAddress_t` for graphics-chain submission, the pause-button
function, Continue callback and its call site. Co-op IDs resolve to the
corresponding campaign planet.

The renderer and pause routines were matched against all 100 extracted retail
boot/campaign/co-op layouts across NTSC-U, PAL, NTSC-J and NTSC-K. Co-op routines
match their campaign counterparts. Runtime checks require the renderer's
expected entry instructions and the original pause call before installation.
Campaign updates/drawing run before `VU1_sendChain`, while its packet buffer
is still writable; the resident sync hook only installs this regional hook.
The native campaign pause renderer stays active underneath the menu. Cleanup
restores only hooks still owned by Mod Menu. Regional builds and
relocation checks pass; live testing is tracked separately from these matches.

The resident player-one controller fallback was verified from native
`InitPadRing(NULL)` references in the same overlays: `0x001ee600` (NTSC-U),
`0x001ee680` (PAL), and `0x00208e40` (NTSC-J/K). librac4 uses these when the
current player-pad pointer is absent, including campaign pause/front-end UI.
Its Make rules pass each region's `EE_DEFS` to C/C++ and preprocessed assembly;
otherwise regional archives silently retain NTSC-U resolver/controller code.

Live campaign checks cover menu drawing, closing and input isolation in
NTSC-U and PAL, plus drawing and Triangle-to-close in NTSC-J. Other planets
and co-op were checked against extracted code, not played through. NTSC-K
has matching address evidence and a passing build, but no live disc test.

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

## Forge boot shortcut (NTSC-U)

The native boot flags are the byte at `0x0021de69`, lobby target at
`0x0021de80`, and skip-intro word at `0x0021edbc`. Startup resets the lobby target,
so setting `eNWLobbyInitToDebug` alone does not reach Local Play staging.

The multiplayer menu's Local Play branch at `0x0074a4dc` requests menu `0x17`.
Create Game's submit branch at `0x00743c64` calls `0x00743da0` with its screen
object, then requests staging menu `0x19`. The submit routine begins with
`0x27bdffb0` and initializes the local roster, rules and map through the native
network setup calls. Forge follows this path once after boot; it does not
initialize an online connection or start the game. Other regions are unverified.

For an explicit PINE request after startup, native `BootMain_CMDMultiCB` is at
`0x004a20c8` (`0x27bdfff0` entry, `0x0c126c68` at +`0x10`). It selects the
Multiplayer route through the boot GUI. Player zero's boot GUI record is at
`0x00306dc0`: active byte +`0x00`, screen ID +`0x04`; BootMain is screen 32.
The visible boot menu can use level 1 with mission -1 (`0x001711a8`), so level
-1 alone is insufficient to identify it. Command 1 requires this active screen,
checks the callback signature, and reports completion after native staging opens.
