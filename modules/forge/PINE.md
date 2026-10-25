# Forge PINE interface v3

Forge owns a **164-byte, 16-byte-aligned mailbox in its module BSS**.
There is no fixed mailbox address or loader-owned reservation.
Use PINE EE memory reads/writes; this is not a host-process pointer. Forge v2
needs no plugin-specific transport. The Forge mod consumes commands on its
normal game-thread update. No editor source changes are included here.

Enable `forge local`, `maploader`, and optionally `modmenu` in modules.txt.
The protocol works without Mod Menu. Currently supported: NTSC-U Deadlocked Local Play
staging and the retail base IDs listed below. Online, campaign, boot menus,
and transitions do not accept new map commands.

## Quick guide: send a command

First enable PINE in PCSX2, boot the game, and enter **Local Play staging** or
a running Local Play game. Discover the mailbox using the pointer chain below;
never hard-code a module or mailbox address. In these examples, `M` means the
discovered mailbox address, and `id` is a fresh nonzero 32-bit request ID.

Every command uses this same procedure:

1. Verify the mailbox header and advancing heartbeat. Require `available != 0`
   at `M + 0x18` and the expected context at `M + 0x19`: `1` for staging,
   `2` for a running game. Wait for any previous command or menu-triggered
   scan/load to finish. Do not send during a transition.
2. Check the one-byte capabilities field at `M + 0x1a`:
   `(capabilities & (1 << command)) != 0` must be true.
3. Write the command number as **one byte** to `M + 0x1b`.
4. Write a **129-byte, zero-filled ASCII buffer** to `M + 0x20`, containing
   the filename or retail ID below. Leave it all zero for an empty argument.
   Optionally write the game mode byte to `M + 0x1f` (see Game modes below).
   Finish all payload writes before the next step.
5. Write `id` as a **four-byte little-endian integer** to `M + 0x08` **last**.
   This submits the request. Do not write any game-owned fields or overwrite
   the whole mailbox.
6. Poll for this exact `id`. If `rejectedId` at `M + 0x14` matches, stop and
   report `rejectError` at `M + 0x1e`. Otherwise wait for `completedId` at
   `M + 0x10` to match. Success requires `state == 3` at `M + 0x1c` and
   `error == 0` at `M + 0x1d`. State `4` means failure; report `error`.
   `acceptedId == id` only means accepted, **not finished**.

Use one command at a time. Do not reset IDs to zero between commands. After
reconnecting, read the existing IDs and choose an unused ID for that session;
do not blindly start at `1`. If a command times out, report it and inspect the
heartbeat/context before retrying; it may still be running. Never automatically
resubmit a Start Game or hop with a new ID while its outcome is unknown.

### Refresh the custom map list

1. Finish copying/building all map files into `dl/` before requesting a scan.
2. In staging or a running game, send **command `2`**, empty argument.
3. Wait for successful completion. The catalog is now refreshed.

Refreshing does **not** reload the current level or start a game. If map content
changed and you want to load it immediately, use command `4` below instead.

### Hop to a different custom map while playing

1. Require context `2` (already in a Local Play game).
2. Send **command `3`** with the map's filename stem, for example `hoven gorge`.
   If it was just added or rebuilt, send **command `4`** instead: it refreshes
   the catalog and then loads that map in one request.
3. Wait for successful completion. The new map is playable in the existing
   game; no return to staging or Start Game command is needed.

Use the stem from `dl/hoven gorge.version`: `hoven gorge`, not the display name
`Hoven Gorge`, not `hoven gorge.wad`, and not a full path. This port currently uses NTSC-U assets only.

### Choose a custom map and start a new game from staging

1. Require context `1` (Local Play staging).
2. Send **command `3`**, argument `hoven gorge`, or **command `4`** if the map
   was added/rebuilt and needs a refresh first.
3. Wait for **successful completion**. The map is selected and its staging
   name is updated, but the game has not started.
4. Send **command `5`**, empty argument, using a **new request ID**.
5. Wait for successful completion. Gameplay is ready on the selected map.

Stop if selection fails; sending Start Game anyway would use the previous
selection. To start whatever map is already selected, skip steps 2 and 3.
Command `5` does not accept a map filename. It applies the requested game mode,
defaulting to CQ, with that mode's objective/spawn flags.

### Hop to a retail map or reload the current map

- **Retail hop:** in a running game, send **command `6`**, argument `"42"`
  for Catacrom Graveyard, and wait for success. The argument is ASCII decimal text,
  not a binary integer. Supported retail IDs are listed below.
- **Retail start:** in staging, send command `6`, wait for success, then send
  command `5` with an empty argument and a new ID.
- **Reload:** in a running game, send **command `1`**, empty argument, and
  wait for success. It reloads the current retail/custom map. For a rebuilt
  custom map whose metadata or file sizes changed, use command `4` with its
  filename instead so the catalog is refreshed before loading.

### Editor integration example

This is pseudocode, not a new script or a PINE library API. `sendAndWait` means
the six-step submission/completion procedure above and returns only on success;
on rejection, failure, or timeout it stops the sequence and reports the result.

```text
refresh button:
	sendAndWait(command = 2, filename = "", id = nextRequestId())

test newly built custom map:
	wait until all build output files are completely written
	if context == 1:                         // staging
		sendAndWait(command = 4, filename = "hoven gorge", id = nextRequestId())
		sendAndWait(command = 5, filename = "", id = nextRequestId())
	else if context == 2:                    // running game
		sendAndWait(command = 4, filename = "hoven gorge", id = nextRequestId())
	else:
		report "Enter Local Play staging or a running Local Play game"
```

Recheck availability/context/capabilities before each submission. Keep the
emulator running while waiting, and rediscover the mailbox after a restart or
savestate load. Numeric states/errors and the full protocol are documented below.

## Layout

Fields use the widths listed below; multibyte integers are little-endian. Strings are NUL-terminated ASCII.
Offsets are relative to the discovered mailbox address. The C declaration is
`modules/forge/include/forge.h`; the ELF symbol is `forgeMailbox`.

### Discovery through the loader and module headers

PINE follows **0xa0000 -> module table -> Forge -> exports -> mailbox**.
No memory scan is needed. All pointers are 32-bit little-endian EE addresses.

1. At `0x000a0000`, verify loader magic `0x444c4c44` at +`0x04`, discovery
   version `1` at +`0x08`, and size `48` at +`0x0c`. Read the module-table
   pointer from +`0x00`. Its location is linker-selected, not fixed at a particular offset.
2. Read the table header: magic `0x444d5442`, version `1`, count (at most 8),
   and entrySize `124`. Wait if the loader has not initialized it yet.
   Entries begin at table + `0x10`. Find `forge.ntscu.bin` in the 112-byte
   NUL-terminated name. Address, size and modes are at entry +`0x70`, +`0x74`,
   and +`0x78`. Check the table is inside the loader/module 0x000a0000-0x00100000 range
   and the full module range is inside EE memory before following it.
   With PCSX2's 128 MiB RAM enabled, modules can be in
   `0x02000000-0x07ffffff`; the loader/table remain in low memory.
   Do not reject Forge solely because its address is above the normal 32 MiB.
3. At Forge's base, verify module magic `0x444d4c44` (`DLMD`) at +`0x04`,
   version `1` at +`0x08`, and size `16` at +`0x0c`. Read the exports
   pointer from +`0x00`. Require a nonzero, four-byte-aligned pointer and a
   complete 16-byte exports structure inside Forge's address/size range.
4. Forge exports contain magic `0x31584746` (`FGX1`) at +`0x00`, version
   `1` at +`0x04`, size `16` at +`0x08`, and the mailbox pointer at
   +`0x0c`. The C declaration is `forgeExports_t` in `modules/forge/include/forge.h`.
5. Require a 16-byte-aligned mailbox entirely within Forge's address/size range.
   Check mailbox magic `0x33475246` (`FRG3`) and the full 164-byte range.
   The mailbox has no version or size fields.
   Re-read discovery pointers and the table entry to detect a restart during
   discovery. Verify heartbeat at mailbox + `0x04` advances, then check
   availability, context and capability bits before submitting work.

The headers and exports table are read-only to the host. The exports and mailbox
pointers exist as soon as Forge is loaded, but the mailbox header is initialized
only when Forge first initializes in Local Play. Loaded does not imply active.
If the header is not ready or the heartbeat is stopped, wait for running Local
Play instead of writing commands. A paused emulator also stops the heartbeat.

Cache addresses only for the current session. Rediscover after restart,
savestate load, module replacement, changed header, or stopped heartbeat.

**Protocol v3 is incompatible with earlier mailbox layouts.** Rebuild/deploy
Forge and Map Loader together and cold-boot. Update external clients before
sending commands. Loader/module/exports discovery headers remain version 1;
the mailbox is identified by its new magic. Forge/Map Loader service ABI is 3.
The mailbox is 164 bytes, including space for 128 filename characters plus NUL.

| Offset | Bytes | Field | Owner / meaning |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | Game: `0x33475246` (`FRG3`) |
| 0x04 | 4 | heartbeat | Game: advances on Forge updates |
| 0x08 | 4 | requestId | **Host: write LAST**, nonzero |
| 0x0c | 4 | acceptedId | Game: most recently accepted request |
| 0x10 | 4 | completedId | Game: most recently completed request |
| 0x14 | 4 | rejectedId | Game: most recently rejected request |
| 0x18 | 1 | available | Game: compatible Map Loader available |
| 0x19 | 1 | context | Game: 0 unavailable, 1 staging, 2 game |
| 0x1a | 1 | capabilities | Game: bit `(1 << command)` |
| 0x1b | 1 | command | Host: command number |
| 0x1c | 1 | state | Game: accepted operation state |
| 0x1d | 1 | error | Game: accepted operation error |
| 0x1e | 1 | rejectError | Game: rejected operation error |
| 0x1f | 1 | gameMode | Host: optional native mode ID; reset to CQ when consumed |
| 0x20 | 129 | filename | Host: up to 128 ASCII characters plus NUL |
| 0xa1 | 3 | padding | Zero; do not write |

Write `command` as **one byte**, not a 32-bit word: adjacent bytes belong to the
game. Write filename separately, then the aligned 32-bit request ID last.

## Game modes

The optional one-byte `gameMode` uses libdl's `GameRuleIds`:

| Value | Mode |
| --- | --- |
| 0 | Conquest (CQ), default |
| 1 | Capture the Flag (CTF) |
| 2 | Deathmatch (DM) |
| 3 | King of the Hill (KOTH) |
| 4 | Juggernaut |

Write it before `requestId`. Forge snapshots and resets it to CQ when consuming
**any** request, including rejections. Omitting the byte on the next request uses
CQ. Wait for acknowledgement before preparing another request.

Commands 1, 3, 4, 5 and 6 apply the mode after the map request validates. Refresh
(command 2) ignores it and leaves the mode unchanged. Refresh-and-select retains
the requested mode while scanning. Unsupported values reject with error 9.
A missing map or rejected request leaves the current mode unchanged.

For a CTF start from staging, send command 3 (or 4) with `gameMode = 1`, wait
for completion, then command 5 with `gameMode = 1` again. For a Deathmatch hop,
send command 3 or 6 with `gameMode = 2`. A reload (command 1) without the byte
switches to CQ. Menu/shortcut reloads and in-game menu hops apply Forge's pending
Game Mode choice, which starts with the mode chosen for the current game.
In staging, the selector updates the staging settings and label immediately.
Mailbox commands use their own byte and ignore the pending menu choice.

When changing modes, Map Loader sets `GameSettings.GameRules` and uses the
native mode preset for `GameOptions.GameFlags`, including objectives, spawns,
teams and mode limits. Weapon flags, point values and upgrade timers are
preserved. A same-mode request keeps the existing game flags. The selected map
must support the requested mode.

Forge's Chargeboots and Infinite Ammo toggles apply independently, default to on,
and survive mode changes, including mailbox requests. Save/Load Settings persists
them; a mode preset does not turn them off.

## Commands

| Number | Command | Staging | In match |
|---|---|---|---|
| 1 | Reload current | Reject: no active game | Reload the current retail or custom map |
| 2 | Refresh | Rescan catalog | Rescan catalog |
| 3 | Select/load filename | Select map and update staging name; user still presses Start | Hop to map |
| 4 | Refresh and select/load filename | Rescan, find and select map | Rescan, find and hop to map |
| 5 | Start Game | Start the current map with the requested mode (CQ by default) | Reject: already in a game |
| 6 | Select/load retail map | Clear custom override and select retail base | Hop to retail base |

Start Game takes no filename. Select a map with command 3 or 4 first if needed,
wait for that request to finish, then submit command 5 with a new ID. Retail maps
also work: whatever is selected in staging is used. The command defers the normal
retail Start Game callback until the next update, allowing Mod Menu to close,
and reports completion only when gameplay is ready. It preserves the game's
normal roster, overlay selection and HUD initialization. Mode changes apply
the native game-flag preset described above. The callback
is signature-checked; unavailable code or invalid selected map files reject the
request. Leaving staging before execution cancels it. It cannot start online games.
Staging capabilities are now `0x7c`; in-game capabilities are `0x5e`.
Older builds without bit `(1 << 5)` do not implement Start Game.

Command 6 (`FORGE_SELECT_RETAIL`) uses the existing filename field as a
NUL-terminated **decimal base-map ID**, for example `"42"`. Only the IDs in the following table are accepted. It takes no host path and does not scan custom maps. Check
capability bit `(1 << 6)` before sending it. In staging, wait for completion and
then send command 5 if you want to start the game. In gameplay, completion means
the retail transition and HUD setup finished. Command 1 subsequently reloads that
retail map; commands 3/4 can switch back to custom maps.

| ID | Retail map |
|---|---|
| 41 | Battledome Tower |
| 42 | Catacrom Graveyard |
| 44 | Sarathos Swamp |
| 45 | Dark Cathedral |
| 46 | Temple of Shaar |
| 47 | Valix Lighthouse |
| 48 | Mining Facility |
| 50 | Torval Ruins |
| 51 | Tempus Station |
| 53 | Maraxus Prison |
| 54 | Ghost Station |

Example filename: `hoven gorge`, not `Hoven Gorge`, `hoven gorge.wad`, or a host path.
Names are case-sensitive, 1-128 printable ASCII bytes. Separators, traversal,
colons, wildcards, leading dots and unterminated strings are rejected. Files must
pass Map Loader's usual map-pack validation. A missing map is an explicit error.
Refresh preserves selection by filename when present; removed maps lose selection.
In-game refresh requires the same mapscan helper as staging. It updates the
catalog only: use command 1 or 4 to load changed map content.

## Submission and completion

1. Read magic, available, context and capabilities. Verify heartbeat
   advances when emulation is running. A stopped heartbeat may simply mean pause.
2. Wait for earlier accepted work to complete. Choose a fresh nonzero request ID;
   never reuse an ID in a session (skip zero on wraparound).
3. Write command and a zero-padded 129-byte filename field. For commands 1/2/5,
   an empty filename is sufficient. Optionally write gameMode; omitted means CQ.
   Wait for these PINE writes to complete.
4. Write requestId **last**, in a separate completed write.
5. Poll: `rejectedId == id` reports rejection; `acceptedId == id` acknowledges
   work; `completedId == id` reports terminal state/error. Do not interpret an
   old completedId or old rejection as the current response.

The mod copies the payload and rechecks requestId before accepting it. Do not
modify payload fields until acknowledged, and use one editor/writer per mailbox.
Repeating the same requestId does not execute it again. A new request while busy
is rejected without replacing active work; its rejection has separate fields.
If accepted, IDs/status are written before the completion marker. Read the
completion marker again around status reads if submitting consecutive requests.

States: 0 idle, 1 scanning, 2 loading/queued transition, 3 complete, 4 failed.
Errors: 0 none, 1 wrong context, 2 busy, 3 unknown command, 4 invalid filename,
5 map not found, 6 scan failure, 7 selection/loading/HUD failure, 8 inactive, 9 invalid game mode.
Rejected requests have separate ID/error fields and do not replace the accepted operation.

Leaving Local Play marks Forge unavailable and cancels its pending response.
Requests posted while inactive are discarded on reactivation. Restarting the
game resets the mailbox; reconnect and recheck the handshake. Savestate rewind
can rewind IDs and heartbeat, so the editor should abandon outstanding requests
and establish a new session after loading a savestate. No network authentication
is implemented here; access control belongs to the emulator's PINE endpoint.

## Verification

The existing mailbox checks cover request ordering, duplicate IDs, busy rejection,
inactivity, 128-character filenames, and unterminated-name rejection. Live NTSC-U
mailbox tests covered selection, Start Game, custom/retail hops, and repeated
reloads. Mode selection was verified with Metropolis CTF and Hoven Gorge CQ,
including their objectives, HUD and gameplay. The same mailbox protocol is used for debugger and PINE memory writes;
editor-side PINE integration still needs its own verification.

Campaign, online, PAL, NTSC-J, and split-screen hopping are unsupported in this
port. See [Forge support notes](README.md#support-and-limitations).
