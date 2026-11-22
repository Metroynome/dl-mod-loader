# Moby replication rewrite

## Scope and status

Replace the generic snapshot experiment, rather than layering a second driver over it. Initial adapter: NTSC-U Catacrom Blade Balls (class 0x099c). Other enemies, pickups, vehicles and destructibles are deliberately outside this first adapter. The existing encounter code remains responsible only for replaying the host's native wake/spawn operations. It is not a second moby state replication backend.

The direct Online module remains the transport. This is a Guber-inspired object replication design, not a claim that Medius or native Guber events are running in campaign. No native netObject pointer is overwritten. Shared Co-op player, camera/HUD, menu and level-transition functionality remains separate.

## Verified native path

Sources: open Sarathos multiplayer EE dump, clean Catacrom Cooperative EE dump, local prototype decompilation, libdl and Horizon Survival. Prototype function names are hints, not proof; several functions have incomplete bodies or misleading names.

Sarathos:
- 0x0061c2b0: reserves a Guber identity, creates class event zero, writes new UID, class, pVar size and parent UID. Creation is an event, not a raw moby memory copy.
- 0x00611338: event creation writes object UID, origin client, event ID, timing and send destination into a pooled event.
- 0x006115a8: event post-update sends pending events through NetSendApplicationMessage (0x01e9e270), adding transport flag 0x40 (delivery critical), then clears send-pending. This does not establish ordering or reliability for our UDP transport.
- 0x006116a0: receive callback allocates and copies event header/payload, ignoring the local origin.
- 0x006114d8: eligible events move to the dispatch list after send/timing conditions; destination filtering occurs before dispatch.
- 0x00611718: dispatch resolves object UID through 0x005ce6e0 and calls its virtual event handler at vtable offset 0x14.
- GuberMoby HandleEvent dispatches to the moby class handler. SpawnInit obtains the class netUpdate callback. Ownership is explicit and native code also supports master migration; migration is excluded from this first two-peer adapter.

Survival mob.c:
- mobHandleEvent validates host/owner before dispatching spawn, destroy, damage, state, target and ownership events.
- mobSendStateUpdateUnreliable sends frequent position/state/target/path updates separately from reliable events.
- The guest applies class-specific state transitions, target references and rendering behavior. Gubers alone do not define enemy behavior.

## Catacrom findings

Native runtime candidates: class resolver 0x005661e0; get/create wrapper 0x00663cc8; event creation 0x00659640. Resolver/wrapper instruction paths were checked. Other candidates need full validation before they could be called.

Snapshot moby pool is 0x01b40740..0x01b74640, from globals 0x00222794 and 0x002227b0. Class tables resolved using the native resolver:
- Blade Ball 0x099c: table 0x003bf8f0, native update 0x00476340.
- Robot zombie 0x20f6: table 0x003bf570.
- DZ striker legs 0x2130: table 0x003bf5d0.
All three have stub GetGuberObject at 0x00539038 (jr ra / move v0,zero) and null class event handlers. Native Guber support is present but these enemy network behaviors are not implemented. Do not attach wrappers and assume replication will follow.

## Replacement design

- Host runs enemy AI, attacks, damage/death decisions and rewards. Guest does presentation only; no duplicate enemy decisions or rewards.
- Registry-owned IDs identify one lifetime. Epoch changes on session/map/mission reset. A recycled native UID/address is not the same replicated object.
- Acknowledged per-object create, critical-state and remove events persist until acknowledged; one missing enemy does not stall every other object.
- Guest binds create events to unambiguous local objects produced by native encounter replay. Do not fabricate native pVars, copy pointers, or guess constructors. An unavailable local object leaves its create unacknowledged for retry.
- Replaceable compact motion records share player packets. They cannot create objects or resurrect removed identities. Guest interpolation runs on its native update callback; animation decoding must also run there, never in packet receipt.
- Dedicated object event channel is the fourth Online channel after player, world and encounter. Keep those responsibilities distinct. Do not add a parallel native Guber dispatcher.
- Old snapshot magic/layout is rejected. Both peers must install matching Co-op builds.

## Validation before expansion

Compile/relocation checks and protocol model checks first. Then: one Blade Ball, host wake, pursuit/attack presentation, host and guest weapon hits as observed by host, host death/removal, repeated spawn lifetimes, disconnect/reset and level transition. Only expand class adapters after that passes. Packet loss, reordering and delayed local binding must not resurrect objects or block unrelated IDs. Full guest hit validation and native class-specific effects require additional inspection; do not claim them from transform synchronization.

## First implementation checkpoint

Implemented in objects.c plus the single bladeball.c adapter. The previous generic object loop/protocol is removed. Four 28-byte motion records fit the existing 128-byte player payload. Critical 64-byte events use a separate channel with sendEvent backpressure (Online API 3) and per-object revision acknowledgements/retries. A session sync handshake replaces the global all-enemies baseline barrier. Registry capacity is 128 lifetimes per mission; IDs are not recycled within that epoch. Exhaustion currently stops discovery rather than risking identity reuse.

Host model checks passed against the actual C backend/adapter: missing/delayed binding, multiple IDs, critical revision acknowledgements, native slot reuse, invalid floats, cross-epoch rejection, duplicate create/state/remove messages, per-object motion sequence checks, interpolation, tombstones and callback ownership. Online queue checks cover channel fairness, busy RPC retention, per-channel reordering and event backpressure. NTSC-U Online and all four Co-op build/relocation checks passed. These are not live gameplay results; only NTSC-U Catacrom is enabled.

The first live test must isolate Blade Balls. Do not expand to zombie/striker adapters or native Guber transport until the new path's creation, animation/pursuit, removal and reset behavior is verified. Normal player-input replication remains how the host simulates guest weapon actions; independent guest hit requests and complete projectile/effect replication are not implemented here.

## Guest update-distance finding

The slideshow report was reproduced with regular incoming motion (roughly 30 player packets per second). Registry targets changed while the active guest mobys stayed at their previous positions. Temporary callback instrumentation, restored immediately afterward, showed callbacks for the dead Blade Balls but not the active ones. Increasing only the active replicas' native updateDist byte at 0x30 from 64 to 255 caused their positions to follow the received targets. Native distance culling was preventing the replica callback from reaching presentation; increasing packet rate would not fix this.

Binding now saves updateDist and sets it to 255 for the replicated lifetime. Reset restores the original distance only while this module still owns that exact lifetime and callback. The actual C model checks cover setting/restoring distance and leaving another callback owner's settings alone. NTSC-U build and relocation comparison passed. Visual animation quality still needs user confirmation; this measurement proves position application, not complete combat replication.

The follow-up live check found all three active identities bound with matching positions, but guest mode 0x0570 retained no-animation (0x40), whereas host mode 0x5530 did not. Both had the same model, 23 joints, scale and bangles. Clearing only that flag on the running guest made animation frames advance (for example 28.5 to 31.5 on one replica), instead of remaining frozen. Presentation now follows the host's no-animation bit; reset restores the original bit alongside the update-enable bits. Do not copy all mode flags: damage decisions remain host-owned.

## Regular player flinch

Live sampling confirmed guest health followed host damage (202 to 192 to 182), but its hudHealthTimer remained zero. Shared health changes now reveal the HUD as native Inc/DecHitPoints do. Reaction replication is separate: the host samples native GET_HIT state entry every module update and retains a per-player counter in repeated world snapshots. The world protocol magic changed; install matching builds on both peers. A new epoch establishes a baseline without replaying historical hits.

The guest consumes each counter once and enters GET_HIT through the already verified native InitBodyState, during the existing pre-draw player pass. Native HeroInitBodyState case 0x16 performs animation, hit sound and actuator feedback. It also reads collDamage and can decrement HP, so the replay clears that transient index to 255 before calling it; health remains host-authoritative. Dead players and pause/menu states are excluded. This first pass supports regular GET_HIT only, not electric, flattened, vehicle or other special reactions; exact host knockback direction is not replicated yet.

Actual C helper checks passed for hit-state edges, duplicate suppression, epoch baselines, unchanged HP, collision-index clearing, death priority and pause exclusion. NTSC-U build/relocation checks passed. Native hit sound/animation/controller feedback still need the restarted gameplay test.

## Inactive guest leftovers

After the flinch test, live inspection found UID 758 and 876 unregistered on both peers. On the host they were state 15, mode 0x0577, collision off, at Z=506 outside play. Guest copies were also state 15 and collision off, but mode 0x0575 and still at their previous playable spawn positions with frozen animations. This is not proof that another object destroyed them; it demonstrates an inactive lifetime missed by per-frame active-object discovery.

The existing encounter wake hook now notifies the object registry immediately after native host wake setup, before a later wave/update can disable it. Discovery remains as a fallback; duplicate notifications do not allocate another identity. Guest presentation hides host-disabled instances (both disabled/no-update bits set) and restores visibility if the same tracked lifetime becomes active. Critical mode changes include the no-animation bit too. Model checks cover an explicitly awakened already-disabled instance, duplicate notification and inactive/active visibility. A fresh session is required to verify this handoff; a previous session's missing registry history cannot be reconstructed by this source change.

## Tested checkpoint — 2026-11-22

The user confirmed attack animations on the guest, improved regular hit reactions, additional synchronized kills, and successful cleanup after both emulators restarted with the latest build. This checkpoint is NTSC-U Catacrom with two PCSX2 peers. It does not establish support for the other enemy classes, regions, full effects, special reactions or exact knockback direction. Earlier sections retain the investigation sequence; their pending-test statements are superseded only for these confirmed checks.
