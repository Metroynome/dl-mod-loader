# Cooperative adapter

NTSC-U test adapter for Cooperative DreadZone Station (21) and Catacrom (22).
Other levels and regions are not enabled yet. It requires Online; Mod Menu is
optional. Build with `make NAME=coop REGION=ntscu` and follow
[Online's setup](../online/README.md). Enable `coop campaign` in modules.txt.
Both systems must use matching Co-op and Online builds.

The adapter starts ON. Host controller 1 owns player 1; guest controller 1 owns
player 2. Each gets a full-screen camera/HUD. Start, Select, L3/R3 and menu input
stay local. Stale remote input becomes neutral. Disconnecting hides the peer
without returning to split-screen. Native hero slots remain allocated.

The host advertises its level and mission. A guest at another supported mission
uses the normal transition. Launch mission indices and loader IDs are sent
separately. The transition does not request a save, but native autosave can still
save afterward; the isolated test cards have backups.

Late joins use native co-op respawn selection and safety checks, including a
small ground footprint and a 16-unit distance limit. If no checked point is
available, spawning waits. Vendors, vehicles and transition states are not valid
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
