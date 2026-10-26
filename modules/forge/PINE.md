# Forge PINE interface v4

Forge owns a **164-byte, 16-byte-aligned mailbox in its module BSS**.
There is no fixed mailbox address or loader-owned reservation.
Use PINE EE memory reads/writes; this is not a host-process pointer. Forge v2
needs no plugin-specific transport. The Forge mod consumes commands on its
normal game-thread update. No editor source changes are included here.

Enable `forge campaign,local`, `maploader local`, and optionally `modmenu` in modules.txt.
The protocol works without Mod Menu. Currently supported: NTSC-U Deadlocked Local Play
staging and the retail base IDs listed below. Command 1 can enter Local Play
from the boot intro/preview or boot menu. Online, loaded campaign levels and transitions do not accept
new commands; map commands also require staging or a running Local Play game.

## Quick guide: send a command

First enable PINE in PCSX2, boot the game, and enter **Local Play staging** or
a running Local Play game (or the boot menu for command 1). Discover the mailbox using the pointer chain below;
never hard-code a module or mailbox address. In these examples, `M` means the
discovered mailbox address, and `id` is a fresh nonzero 32-bit request ID.

Every command uses this same procedure:

1. Verify the mailbox header and advancing heartbeat. Require `available != 0`
   at `M + 0x14` and the expected context at `M + 0x15`: `1` for staging,
   `2` for a running game, `3` for the boot menu. Wait for any previous command or menu-triggered
   scan/load to finish. Do not send during a transition.
2. Check the one-byte capabilities field at `M + 0x16`:
   `(capabilities & (1 << command)) != 0` must be true.
3. Write the command number as **one byte** to `M + 0x20`.
4. Write a **129-byte, zero-filled ASCII buffer** to `M + 0x22`, containing
   the filename or retail ID below. Leave it all zero for an empty argument.
   Optionally write the game mode byte to `M + 0x21` (see Game modes below).
   Finish all payload writes before the next step.
5. Write `id` as a **four-byte little-endian integer** to `M + 0x1c` **last**.
   This submits the request. Do not write any game-owned fields or overwrite
   the whole mailbox.
6. Poll for this exact `id`. If `rejectedId` at `M + 0x10` matches, stop and
   report `rejectError` at `M + 0x19`. Otherwise wait for `completedId` at
   `M + 0x0c` to match. Success requires `state == 3` at `M + 0x17` and
   `error == 0` at `M + 0x18`. State `4` means failure; report `error`.
   `acceptedId == id` only means accepted, **not finished**.

Use one command at a time. Do not reset IDs to zero between commands. After
reconnecting, read the existing IDs and choose an unused ID for that session;
do not blindly start at `1`. If a command times out, report it and inspect the
heartbeat/context before retrying; it may still be running. Never automatically
resubmit a Start Game or hop with a new ID while its outcome is unknown.

### Enter Local Play when automatic boot is off

1. Enable Forge for `campaign,local` and cold-boot with `bootToLocalPlay=0`.
   You can send the command during the boot intro/preview or at the
   Campaign/Multiplayer menu. Wait for Forge's mailbox and capability bit first.
   The request uses startup flags, deferring the native menu callback until safe;
   it does not change the saved automatic-boot preference.
2. Discover the mailbox. Require context `3` and capability bit `0x02`.
3. Send **command `1` (`FORGE_LOCAL_PLAY`)**, empty filename, using the normal
   payload-first/request-ID-last procedure. The game-mode byte is ignored.
4. Wait for successful completion. Native Local Play staging is now open;
   no game has started. You can then select a map and send Start Game below.

This is a one-time request, not a configuration change. The saved boot toggle
stays off. Map Loader and Mod Menu are not required for this command. The request
survives the brief unavailable context while multiplayer loads. Do not resend it
because availability or heartbeat briefly stops during that transition.

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
4. Send **command `6`**, empty argument, using a **new request ID**.
5. Wait for successful completion. Gameplay is ready on the selected map.

Stop if selection fails; sending Start Game anyway would use the previous
selection. To start whatever map is already selected, skip steps 2 and 3.
Command `6` does not accept a map filename. It applies the requested game mode,
defaulting to CQ, with that mode's objective/spawn flags.

### Hop to a retail map or reload the current map

- **Retail hop:** in a running game, send **command `5`**, argument `"42"`
  for Catacrom Graveyard, and wait for success. The argument is ASCII decimal text,
  not a binary integer. Supported retail IDs are listed below.
- **Retail start:** in staging, send command `5`, wait for success, then send
  command `6` with an empty argument and a new ID.
- **Reload:** in a running game, send **command `7`**, empty argument, and
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
		sendAndWait(command = 6, filename = "", id = nextRequestId())
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
   Check mailbox magic `0x34475246` (`FRG4`) and the full 164-byte range.
   The mailbox has no version or size fields.
   Re-read discovery pointers and the table entry to detect a restart during
   discovery. Verify heartbeat at mailbox + `0x04` advances, then check
   availability, context and capability bits before submitting work.

The headers and exports table are read-only to the host. The exports and mailbox
pointers exist as soon as Forge is loaded, but the mailbox header is initialized
only when Forge first initializes in an allowed context. Loaded does not imply
active. If the header is not ready or the heartbeat is stopped, wait for the
boot menu or running Local Play. A paused emulator also stops the heartbeat.

Cache addresses only for the current session. Rediscover after restart,
savestate load, module replacement, changed header, or stopped heartbeat.

Deploy Forge and Map Loader together and cold-boot. Loader/module/exports
discovery headers use version 1; Forge/Map Loader service ABI is 3.
The mailbox is 164 bytes, including space for 128 filename characters plus NUL.

| Offset | Bytes | Field | Owner / meaning |
| --- | --- | --- | --- |
| 0x00 | 4 | magic | Game: `0x34475246` (`FRG4`) |
| 0x04 | 4 | heartbeat | Game: advances on Forge updates |
| 0x08 | 4 | acceptedId | Game: most recently accepted request |
| 0x0c | 4 | completedId | Game: most recently completed request |
| 0x10 | 4 | rejectedId | Game: most recently rejected request |
| 0x14 | 1 | available | Game: Map Loader or boot command backend available |
| 0x15 | 1 | context | Game: 0 unavailable, 1 staging, 2 game, 3 boot menu |
| 0x16 | 1 | capabilities | Game: bit `(1 << command)` |
| 0x17 | 1 | state | Game: accepted operation state |
| 0x18 | 1 | error | Game: accepted operation error |
| 0x19 | 1 | rejectError | Game: rejected operation error |
| 0x1a | 2 | alignment padding | Do not write |
| 0x1c | 4 | requestId | **Host: write LAST**, nonzero |
| 0x20 | 1 | command | Host: command number |
| 0x21 | 1 | gameMode | Host: optional native mode ID; reset to CQ when consumed |
| 0x22 | 129 | filename | Host: up to 128 ASCII characters plus NUL |
| 0xa3 | 1 | padding | Zero; do not write |

All host-editable fields are grouped at `0x1c` through `0xa2`. Command, gameMode
and filename form one contiguous 131-byte payload starting at `0x20`. You may
write that payload together, or write each field separately using its listed
width. Complete those writes before writing the aligned request ID at `0x1c`
**last**. Never submit the entire request block with a new ID in one write; the
game could consume it before the payload is complete.

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

Commands 3, 4, 5, 6 and 7 apply the mode after the map request validates. Refresh
(command 2) and Enter Local Play (command 1) ignore it. Refresh-and-select retains
the requested mode while scanning. Unsupported values reject with error 9.
A missing map or rejected request leaves the current mode unchanged.

For a CTF start from staging, send command 3 (or 4) with `gameMode = 1`, wait
for completion, then command 6 with `gameMode = 1` again. For a Deathmatch hop,
send command 3 or 5 with `gameMode = 2`. A reload (command 7) without the byte
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

| Number | Command | Boot menu | Staging | In game |
|---|---|---|---|---|
| 1 | Enter Local Play | Open Local Play staging | Reject | Reject |
| 2 | Refresh | Reject | Rescan catalog | Rescan catalog |
| 3 | Select/load custom map | Reject | Select custom map | Hop to custom map |
| 4 | Refresh and select/load custom map | Reject | Rescan and select custom map | Rescan and hop to custom map |
| 5 | Select/load retail map | Reject | Clear custom override and select retail base | Hop to retail base |
| 6 | Start Game | Reject | Start the selected map | Reject |
| 7 | Reload current map | Reject | Reject | Reload the current retail/custom map |

Start Game takes no filename. Select a map with command 3, 4 or 5 first if needed,
wait for success, then submit command 6 with a new ID. The native Start Game
callback runs on the next update so Mod Menu can close first. Completion means
that gameplay is ready. Leaving staging before execution cancels the request;
it cannot start online games. Game-mode changes use the native presets above.

Capabilities: boot menu `0x02`, staging `0x7c`, in game `0xbc`.
Always check `(capabilities & (1 << command)) != 0` before submitting.

Command 5 (`FORGE_SELECT_RETAIL`) uses the filename field as a NUL-terminated
**decimal base-map ID**, such as `"42"`. It takes no host path and does not scan
custom maps. In staging, wait for success before sending command 6 to start.
In gameplay, success means the transition and HUD setup finished. Command 7
then reloads that retail map; commands 3/4 can switch back to custom maps.

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

For custom maps, use the filename stem: `hoven gorge`, not `Hoven Gorge`,
`hoven gorge.wad`, or a host path. Names are case-sensitive, 1â€“128 printable
ASCII bytes. Separators, traversal, colons, wildcards, leading dots and
unterminated strings are rejected. Files must pass Map Loader validation.
A missing map is an explicit error. Refresh preserves selection by filename
when present; removed maps lose selection. In-game refresh requires mapscan.
Refresh only updates the catalog: use command 7 or 4 to load changed content.

## Submission and completion

1. Verify magic, advancing heartbeat, availability, context and capabilities.
2. Wait for earlier accepted work to finish. Choose a fresh nonzero request ID.
3. Write command and a zero-padded 129-byte filename. Commands 1/2/6/7 use an
   empty filename. Optionally write gameMode; omitted means CQ.
4. Complete those writes, then write requestId **last**, in a separate write.
5. Poll for that exact ID: rejectedId reports rejection, acceptedId acknowledges
   work, and completedId reports terminal state/error. Ignore earlier IDs.

The mod snapshots the payload and rechecks requestId before accepting it.
Keep payload fields unchanged until acknowledgement and use one writer per
mailbox. Duplicate IDs do not execute again. Requests while busy are rejected
without replacing active work; rejections have separate ID/error fields.
Read completedId again around status reads to avoid mixing consecutive results.

States: 0 idle, 1 scanning, 2 loading/queued transition, 3 complete, 4 failed.
Errors: 0 none, 1 wrong context, 2 busy, 3 unknown command, 4 invalid filename,
5 map not found, 6 scan failure, 7 selection/loading/HUD failure, 8 inactive,
9 invalid game mode.

Leaving Local Play cancels pending map responses. Command 1 retains its response
across its own boot-to-multiplayer transition. Requests posted while inactive
are discarded on reactivation. A restart resets the mailbox; reconnect and
rediscover it. After a savestate rewind, abandon outstanding requests and
establish a new session. Access control belongs to the emulator's PINE endpoint.

## Verification

Mailbox checks cover ordering, duplicate IDs, busy rejection, inactivity,
128-character filenames, unterminated names, and boot-request completion across
context changes. Live NTSC-U tests covered selection, Start Game, custom/retail
hops and repeated reloads. Metropolis CTF and Hoven Gorge CQ were checked with
objectives, HUD and gameplay. Enter Local Play was tested through PINE with
automatic boot off, preserving that saved setting. Those live checks preceded
this command renumbering; Forge v2's client should use the command IDs above.

Campaign, online, PAL, NTSC-J and split-screen hopping remain unsupported.
See [Forge support notes](README.md#support-and-limitations).
