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

Horizon Deadlocked's plugin uses the `sceGsSyncV` return at `0x00138dfc`.
All four extracted cores contain `ld s0,0(sp)` at `0x00138df8`, `jr ra` at the
hook, and `addiu sp,sp,32` at `0x00138e00`. The PNACH checks the surrounding
instructions before replacing the return with the loader jump. Campaign uses its existing cheat-template return at `0x00139a48`, preceded by
`ld s0,0(sp)` and followed by `addiu sp,sp,64`. Both sites match all four cores.
Each wrapper passes its intended mode; mismatching hooks do not dispatch.
A live NTSC-U menu test showed Horizon's hook initializing but not continuously
updating at boot, so the campaign hook is required for recurring callbacks.

| Region | Current level global (librac4/include/game.h) |
|---|---|
| NTSC-U | 0x0021de10 |
| PAL | 0x0021de90 |
| NTSC-J / NTSC-K | 0x00238610 |

Level IDs come from `RAC Levels/rac-defs.json`, game 4: main menu -1;
campaign planets 1?15 (sparse), co-op +20, multiplayer lobby 39, multiplayer
planets +40, splitscreen +60. `librac4` additionally recognizes level 0 as a
menu state. The adapter enumerates these IDs instead of treating arbitrary
unknown values as valid gameplay. Library functions still need their own
player/overlay readiness checks.

`tools/verify_addresses.py` checks SDK entry words and the hook sequence against
all four regional boot cores. This does not establish complete runtime or
library correctness. The available libdl is NTSC-U only; the loader rejects
other region/library combinations. No heap or allocator edits are performed.

Live NTSC-U PCSX2 verification confirms the boot menu level is -1, the five SDK
signatures match, both expected modules initialize, and the campaign hook keeps
the hello counter advancing after initialization. Multiplayer gameplay and
non-US runtime transitions remain untested.
