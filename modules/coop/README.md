# Cooperative adapter

NTSC-U test adapter for Cooperative DreadZone Station (21) and Catacrom (22).
Other levels and regions are not enabled yet. It requires Online; Mod Menu is
optional. Build with `make NAME=coop REGION=ntscu` and follow
[Online's setup](../online/README.md). Enable `coop campaign` in modules.txt.
Both systems must use matching Co-op and Online builds.

## Cooperative boot menu

With Co-op loaded, Cooperative opens Split Screen, Host and Join. Campaign's
normal New Game/Load Game menu is unchanged.

- Split Screen opens the original Cooperative save menu and disables the online
  adapter, keeping the native two-player screen layout.
- Host and Join each have four saved connection profiles. Filled entries currently
  display "localhost"; empty slots show [EMPTY]. Select a filled entry to connect,
  or an empty slot to create a profile. The editor has NAME, IP ADDRESS, PORT and
  CREATE for new profiles or SAVE for existing ones. NAME is a placeholder
  until a boot-compatible keyboard is added.
- EDIT and DELETE open a profiles-only list for choosing an entry. Square edits
  the highlighted profile directly; Circle deletes it. Triangle cancels an edit
  or returns from an action list. The footer shows the controller shortcuts.
  CREATE/SAVE writes the profile. Triangle warns before discarding unsaved changes.
  Host profiles use `coop-host.<region>.ini`; existing Host settings become the
  first Host profile. Join profiles remain in `coop-online.<region>.ini`.
- Hosting shows connection progress and opens the listening port before opening
  New Game/Load Game. Backing out cancels hosting.
- Joining first connects to the selected host.
  Joining shows connection steps: preparing, loading drivers, obtaining a local IP
  address and contacting the host. Cancel stops the attempt. Failures keep the
  last step and error code visible until acknowledged. Each step is also logged;
  driver startup logs each filename before loading it.
  Only a successful handshake opens the save-overwrite disclaimer. Cancel
  disconnects; Continue opens the original New Game and Load Game choices.
  The checked connection stays active while choosing/loading the save. If it
  times out during loading, the guest retries once its level is ready.
  Disconnect in the Online tab cancels a pending retry.

The native menus, keypad input capture and rendering do not require Mod Menu.
Existing native controller requirements still apply. The network prototype still
supports NTSC-U Station/Catacrom only; these UI changes do not expand world sync
or copy the host's entire save. Native autosave may write the guest's session.
The guest chooses New Game or Load Game as usual, but its first level transition
waits for a fresh Station or Catacrom host snapshot. The host's current mission
is loaded directly; the guest's saved planet is not visited. Transitions and stale
snapshots keep the loading popup open. Cancel stops before the level transition.
This reuses native save/co-op initialization; it does not copy the host's entire
save, inventory or progression yet.

Mission and planet selection are host-controlled. A joining guest follows the
native screen and highlighted item, without activating its selection callbacks.
The host's normal mission transition loads the next mission on both systems.
Ordinary Start/pause and vendor screens remain independent. This observer path
still needs live verification; it currently covers Station and Catacrom only.

## Connection UI

Co-op owns the optional MOD MENU -> Online tab; Online remains the transport.
Enable both modules to use the connection panel.
Update Co-op, Online and Mod Menu together for the new field-row support.
Mod Menu and Co-op each suppress the boot-menu preview-trailer countdown while
active, so the movie cannot interrupt a menu that owns controller input.

- IP Address is a normal highlighted row. Press X to open its numeric popup;
  Left/Right do not change the address. An unset address displays `[none]` and
  opens as `0.0.0.0`.
- Host Port and Local Port support Left/Right adjustment, including held-button
  repeat. X opens the same popup in Port mode, limited to 1 through 65535.
- D-pad or left stick selects a key; holding a direction repeats after 0.75 seconds,
  then once every 0.1 seconds. X inserts
  the selected key once per press. L1/R1 moves the text cursor;
  Square or the wide Delete key removes the character before it. Done, Start or
  R2 validates/applies; Triangle cancels without changing the old value. The
  cursor starts at the end of the existing value; editing never clears it automatically.
  The cursor blinks while idle and becomes visible immediately after editing.
- Number/punctuation keys are square in render pixels. The panel, input field
  and paired Delete/Done row follow the same keypad width.
- Keypad sounds match the native multiplayer profile keyboard: open `0x07`,
  close `0x08`, navigate `0x15`, insert `0x16` and delete `0x1a`.
- Right stick moves the whole popup, stopping its outer frame at the screen
  edges. Small stick drift is ignored; reopening centers the popup.
- The PS5-style keypad keeps both `.` and `-` visible. IP enables only `.`,
  Port disables both, and Number enables both for values such as `-1.06`.
  IP input requires four numbers from 0 through 255.
- Load Settings and Save Settings are side by side. Saved defaults load on
  startup from `host0:dl/config/coop-online.ntscu.ini`. A fresh configuration has
  no IP address, Host Port 25565 and Local Port 25566. Edits affect the next
  Host/Join request, not the current connection.

The implementation is under `ui/`. `numpad.h` exposes IP Address, Port and Number
input types. Open takes initial text and numeric minimum/maximum limits. Number
accepts signed decimal values; `coopNumpadNumber()` returns the accepted value,
while `coopNumpadText()` preserves its spelling. `coopNumpadValue()` returns IP
bytes or an integer port. Input holds up to 31 characters (15 for IP, 5 for Port).
The numpad has no Mod Menu dependency.
Standalone callers can feed raw right-stick X/Y bytes (centered at 127) to
`coopNumpadMove` each frame. Mod Menu supplies these through `getRightStick`,
using the original captured input before gameplay controls are blocked.
Supply the left stick to `coopNumpadNavigate` before the input update for the same
key navigation and repeat behavior. D-pad input takes priority over the stick.

The upcoming standalone UI can call `coopConnectionOpen`,
`coopConnectionInput` and `coopConnectionDraw`, or use the numpad directly.
Its caller must capture game input, supply both new presses and held buttons, draw once per
frame before native graphics-chain submission, and keep closing buttons blocked
until released. There is no standalone opening shortcut. Load/Save/Request are
also callable without Mod Menu; initialize through `coopConnectionInit` first.
The current Mod Menu bridge supplies capture, safe drawing and release handling.

## Disconnect and pause controls

Triangle back from save selection or the Host/Join address screens disconnects
first, showing "Disconnecting..." until shutdown completes. There are no separate
Disconnect buttons on those boot screens. In-game pause menus retain DISCONNECT
and HOST OPTIONS for a host. Host Options currently contains
Back; later host settings can be added there. MORE keeps the original pause
options reachable within the native eight-row limit, including MOD MENU.

Options > Load is disabled during a session. Confirming Quit disconnects before
returning to the main menu; choosing No keeps the session. Selecting Single Player
or Multiplayer at boot also disconnects first. Disconnect leaves the loaded level
open with neutral remote input and preserves saved connection settings.

## Player and world state

The adapter starts ON. Host controller 1 owns player 1; guest controller 1 owns
player 2. Each gets a full-screen camera/HUD. Start, Select, L3/R3 and menu input
stay local. Stale remote input becomes neutral. Both native heroes remain visible
and allocated when a peer disconnects; the full-screen layout is retained.

The host advertises its level and mission. A guest at another supported mission
uses the normal transition. Launch mission indices and loader IDs are sent
separately. The transition does not request a save, but native autosave can still
save afterward; the isolated test cards have backups.

Late joins use native co-op respawn selection and safety checks, including a
small ground footprint and a 16-unit distance limit. If no checked point is
available, placement waits without blocking peer input or position updates.
Vendors, vehicles and transition states are not valid
spawn points. Position corrections preserve native movement history.

## Catacrom object test

The host runs the original logic for these initial classes:

- Bladeballs, robot zombies and DZ strikers.
- Health containers/orbs, bolt crates and ammo crates.
- Destructible barricades.

The guest matches local objects by UID and class and substitutes a replica
update. Host snapshots supply position, rotation, state, health, visibility and
collision activation. Animation IDs, frames and speeds use the guest model�s native sequence transition, without running its enemy AI. Animation decoding is deferred to the native moby update; packet receipt only queues the change. The initial batches are acknowledged and retransmitted. Ongoing changes use repeated snapshots without waiting for per-batch acknowledgements; positions blend between updates. Up to two enemy records travel alongside each player snapshot, so enemy traffic does not take separate input-transmission slots. Moving animations run locally between sequence changes; static pickups do not send animation frames. Duplicate/older packets are ignored. Recycled local slots and ambiguous IDs are
rejected. UID-zero static health orbs additionally require matching map slots and starting positions. A maximum of 128 objects is tracked. Unchanged acknowledged records are skipped after the initial pass.

After a complete accepted object pass, the guest copies host bolts, shared XP and points. Host health for both players updates independently, even if an object cannot be matched. Host deaths enter the native body state at the next draw boundary so normal death/respawn logic can run. These changes still need a live combat check. This does not copy inventory or mission-objective internals.

This is an initial replica test, not full world synchronization. It cannot
create missing objects or replace an object's class. Vehicles, projectiles,
platforms, shields, enemy-specific internal variables,
mission completion and save persistence need further work.
Disconnecting releases replica callbacks and restores their pre-replica logical
state; preserving an entire host world after disconnect is not implemented.

## Catacrom encounter test

The host captures native wake decisions for the adapted enemy classes, including
the owning controller/wave, spawn reference, position and spawn-time random seed.
The guest resolves its own map objects by UID and replays native wake setup from
the encounter update. The seed is restored afterward; it does not replace the
random stream for the whole game. Duplicate events are acknowledged without
replaying them. Dormant enemies retain their native setup until activated.

This first test handles existing map enemies, with a bounded history of 128 wake
events per loaded mission. Missing dynamic objects are rejected after a timeout;
creating them, syncing full wave progression and adapting other enemy classes
remain unfinished. Ongoing enemy snapshots still provide host-owned movement,
health and animation corrections. This does not make independent AI deterministic.
Only Catacrom's NTSC-U encounter hooks are enabled. The other maps now have
static address mappings, but their encounter behavior still needs live testing.

## Verification

Station-to-Catacrom following, matching players, full-screen cameras and HUDs
were verified in PCSX2. The initial 44-object pass, including both UID-zero health orbs, was accepted with no missing matches. Combat and animation checks are still pending. The NTSC-U build, relocation and mocked packet/lifetime checks pass.
The selected player's view is drawn on the first native pass, before the game's inter-pass render helper. Waiting for player 2's original pass caused stale drawing and Start-menu trails on the guest.
Live adapter disable/split-screen restoration remains unverified after an
earlier native renderer fault. Native Online Play is not supported by this
adapter.

Test guest keyboard: WASD movement, IJKL camera, X Cross, Z Square, C Circle,
Backspace Triangle, Enter Start, Q/E L1/R1, 1/3 L2/R2, F/H L3/R3. Guest SDL input
is disabled so the host's physical controller does not control both instances.

Full-screen HUD getters remain overridden while the co-op view is active, since native GUI initialization resets the player layout fields. The refreshed HUD layout and Bladeball replication still need visual verification.

## Native address tables

The full `VariableAddress_t` tables cover all 12 Cooperative overlays in NTSC-U, PAL, NTSC-J
and NTSC-K:
Station, Catacrom, Sarathos, Kronos, Shaar, Valix, Orxon, Torval, Stygia,
Maraxus, Ghost Station and DreadZone Interior. These addresses were matched
against the retail 1.00 code images identified by `rac-defs.json` in RAC Levels.
They are static mappings, not evidence of live support on every map. The adapter
remains restricted to Station and Catacrom; encounter replay remains Catacrom-only.

General player, moby, pad and game-state tables live in librac4 (`player.h`,
`moby.h`, `pad.h`, `game.h`). The resident random seed is exposed by `math.h`.
The address tables use the `vaCoop` prefix because Cooperative overlays differ from
campaign overlays, even though the address resolver uses the same map slots.
Both library and module-specific tables contain all four regional mappings;
the network adapter is still NTSC-U-only. Level changes reuse librac4's existing
`vaSwitchToLevelFunc`. Camera/HUD hooks, mission bookkeeping and encounter
replication hooks stay in this module.

Some maps do not contain the enemy constructors hooked by `vaWakeCallA/B/C`.
Those entries stay zero; every direct native wake call in each regional image was
accounted for. Do not enable a map by copying another map's missing entries.

Co-op requires the librac4 revision pinned in `docker-init.sh`. For local library
development, build against the sibling checkout:

```sh
docker run --rm -v "$PWD\:/src" -v "${PWD}/../librac4:/deps/librac4" -w /src dl-mod-loader make REGION=all LIBRAC4_DIR=/deps/librac4
```
