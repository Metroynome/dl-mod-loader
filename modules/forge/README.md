# Forge

NTSC-U online handoff was tested through Horizon patch loading/menu use, logout
to Local Play, and PINE Hoven Gorge selection, Start Game and reload. The offline
modules deactivate online; their map commands become available again after logout.

Forge currently provides Local Play map-testing controls for NTSC-U Deadlocked. It uses Map
Loader to reload or change maps without leaving the current game. No changes
to Horizon Forge v2 are included; its developer can integrate the documented
[PINE command interface](PINE.md).

## Setup and use

Forge and its dependencies are enabled in the NTSC-U release. For a custom
module list, use:

```text
modmenu local
maploader local
forge campaign,local
```

Restart emulation after installing binaries or changing `modules.txt`. See the
[root build instructions](../../README.md#build-with-docker).

- **Reload Current Map:** open MOD MENU from the in-game pause menu, then choose
  Forge â†’ Reload Current Map. Works with retail and custom maps.
- **Change maps:** choose a named map in the in-game Custom Maps list and press
  Cross. The menu closes and a normal loading transition begins.
- **Game Mode:** starts with the mode chosen for this game. Scroll left/right
  through the named modes. In staging, the mode label/settings update immediately
  and Start Game uses that choice. During gameplay, Reload Current Map, the
  reload shortcut, or an in-game map hop applies the pending choice.
- **Boot to Local Play:** off by default. Save Settings to retain the toggle,
  then cold-boot to apply it. Forge skips into multiplayer, follows the native
  Local Play/Create Game path, and stops at staging without starting the game.
  It runs once per boot; exiting multiplayer later does not redirect you again.
  Turning it off keeps normal startup. Allow both `campaign,local` contexts so
  Forge can set the boot flags before multiplayer loads.
  PINE command 1 can request the same route from the boot menu while the saved
  option is off, without changing that setting.
- **Chargeboots / Infinite Ammo:** separate toggles, both on by default. Forge
  enforces their game-rule values in staging and gameplay, across starts, reloads
  and hops. Turn them off in Forge to disable them. Chargeboots uses the game's
  Spawn With Chargeboots rule, which grants them when the player spawns.
- **Refresh Custom Maps:** use the Forge tab in staging or supported gameplay
  after changing map files. Refresh updates the catalog; it does not reload the
  current level. Without Forge, press Square on Custom Maps in Local Play staging.

Retail and custom maps share an alphabetical list. `None` is staging-only;
choose a named retail entry to leave a custom map during gameplay. The in-game
list is available only while Forge is active. L3 + R3 remains disabled during
gameplay. Press **L1 + D-pad Up + Circle** during gameplay to reload the current
map without opening Mod Menu. The shortcut works with retail and custom maps,
is disabled while menus/loading are active, and triggers once per press.

Hops use the existing game session and settings, with player recreation and HUD
setup. They are full loading transitions, not seamless geometry updates or a
promise to preserve transient player state, scores, or timers.

## External commands

Forge's tab includes **Quick Load Maps**, **Game Mode**, **Save Settings**, and **Load Settings**.
Quick Load Maps defaults to off. When enabled, it bypasses loading VSync only
for custom-map starts, hops, and reloads. Retail maps retain normal VSync.
Save Settings writes the boot option, Quick Load Maps, the mode choice and both gameplay toggles to
`host0:dl/config/forge.ntscu.ini`; Load Settings restores it. Saved settings are
also loaded when Forge initializes, without requiring Mod Menu. Loading settings
changes the mode immediately in staging, or queues it during gameplay; the two
toggles apply immediately. Each new game starts with its own chosen mode.
Defaults are Boot to Local Play off, Quick Load off, Chargeboots on, and Infinite Ammo on.

The module-owned mailbox accepts:

| Command | Purpose |
| --- | --- |
| Enter Local Play | Enter staging from the boot menu without enabling automatic boot |
| Refresh | Rescan available map packs |
| Select/load filename | Select in staging; hop during gameplay |
| Refresh and select/load | Rescan before selection/hopping |
| Select/load retail | Select or hop to a supported retail base |
| Start Game | Start from staging with current map/settings |
| Reload current | Reload the active retail/custom map |

To start a newly selected map, wait for selection to complete, then send Start
Game with a new request ID. Accepted work is not necessarily finished: always
check completion and error fields. Busy or unsupported requests are rejected.

PINE works without Mod Menu. Discover the mailbox through the loader/module
headers; never hard-code its address. [PINE.md](PINE.md) defines the exact layout,
commands, ordering, readiness checks, and error codes. Map commands were tested
with debugger EE writes; Enter Local Play was also verified through PINE with
the saved boot option off. Forge v2's integration remains editor-side work.

## Support and limitations

- NTSC-U Deadlocked Local Play staging and the retail bases listed in [PINE.md](PINE.md).
- No campaign, online, PAL, NTSC-J, or split-screen hopping support yet.
- No automatic file watching, build-triggered reload, or position restoration.
- Finish writing map files before scanning or loading. A failure after teardown
  may require restarting the game.
- Mod Menu is optional for commands and the reload shortcut. Map Loader is
  required for map commands, but not Enter Local Play.

Live NTSC-U tests covered Hoven Gorge and Metropolis, retail Battledome Tower
and Catacrom Graveyard, repeated reloads, and returning to staging. Gameplay,
HUD, minimaps, loading-screen names/backgrounds, and the reload shortcut were
checked. Other maps and player counts still need individual verification.

## Ownership

Map Loader owns catalog scanning, validation, asset loading, and transitions.
Forge exposes its active capability, menu actions, and command mailbox through
the existing loader service API. Mod Menu supplies UI and input capture.
Deactivation removes Forge's tab and makes the mailbox unavailable. PINE does
not override the loader's offline context restrictions.

## Future suggestions

These are ideas, not implemented features:

- **Reload after a successful build:** an opt-in editor notification triggers a
  reload only after all assets are complete. Queue/coalesce notifications while busy.
- **Remember test position:** restore position and camera after same-map reloads,
  with normal-spawn fallback when geometry changes.
- **Richer status/errors:** show build IDs, missing assets, wrong regions, incomplete
  builds, and load duration so testers can identify what actually loaded.
- **Automatic update detection:** prefer explicit completed-build notifications
  over periodic scans. A revision marker or optional slow scan could support
  older exporters. Changed size/version alone cannot detect every content edit.

For editor export, agree on a complete-build handoff with region, base map,
stable filename, build ID, and asset sizes/hashes. Publish readiness only after
all writes finish; avoid reading partially replaced files. Compute hashes on
the PC. Keep manual refresh/reload available without an editor connection.
Supporting files may use `dl/mods/utils/forge/`; map packs currently remain in
`dl/`. The implemented mailbox protocol is the starting point for integration,
not a proposed replacement protocol.

## Filename/protocol update

Map filename stems now accept up to 128 ASCII characters (plus the NUL terminator).
Update Forge, Map Loader, and `mods/utils/mapscan.irx` together, then cold-boot.
External clients must use [PINE protocol v4](PINE.md); earlier field offsets are incompatible. The mailbox no longer contains version/size
fields, and accepts an optional game-mode byte (CQ by default).
