# DL Mod Loader

Extract the ZIP for your region, or use the `all` ZIP. No build tools are needed.

## Install

1. Copy your region's PNACH into PCSX2's cheats folder and enable DL Mod Loader.
2. Enable host filesystem access and set its root to the folder containing `dl/`.
3. Edit `dl/config/modules.txt`, then fully restart without a savestate.
   `dl/modules.txt` is a fallback; lists are not merged.
4. Upgrade the loader and module binaries together. Preserve `dl/config/*.ini`.

| Region | Serial | CRC | Included support |
| --- | --- | --- | --- |
| NTSC-U | SCUS-97465 | 9BFBCD42 | Campaign/co-op and Local Play |
| PAL | SCES-53285 | D697D204 | Campaign/co-op |
| NTSC-J | SCPS-15100 | 2EC9DA96 | Campaign/co-op |

Older PCSX2 builds may need each file renamed to `<CRC>.pnach`. NTSC-K has a
source build, but no CRC-verified Korean release package. Regional builds do
not mean every planet/player count has been tested in-game.

## Menu and Forge

Release defaults enable Mod Menu and BIOS Warning, plus Map Loader and Forge
on NTSC-U. Test/example modules are excluded. L3 + R3 opens Mod Menu in front-end
menus; during gameplay use MOD MENU from the pause menu.

Forge supports map hopping/reloading, game-mode selection, and PINE commands.
Boot to Local Play defaults to OFF, so the game boots normally. PINE can enter
staging on demand. Chargeboots and Infinite Ammo default to ON; Quick Load Maps
defaults to OFF and only bypasses VSync for custom-map starts, hops and reloads.
Press L1 + D-pad Up + Circle during gameplay to reload once per press.
Forge's Free Cam defaults to ON: press L1 + D-pad Up in a Local Play game
to enter/exit. Square toggles Render All; L1/R1 select fast/slow movement.
Free Cam and its Airwalk/lock preferences can be saved; camera bookmarks are
not saved. Other patch.bin mods can conflict with camera/occlusion behavior.
The Forge tab appears at boot and in Local Play, and stays hidden in campaign.
Game Settings and Free Cam Settings headings use Horizon DL's white text.
Scrolling only moves the viewport when selection crosses a visible edge.
Free Cam can be armed in staging but only activates during gameplay. Forge
and Map Loader release camera input/hooks before starts, reloads and hops
recreate the player. Upgrade Forge, Map Loader and Mod Menu together.
PINE Enter Local Play also accepts requests during the boot intro/preview.
Save/Load Settings uses `dl/config/forge.ntscu.ini`; existing INI choices win.

Offline modules deactivate online so Horizon can take over. NTSC-U tests covered
Horizon patch loading/menu use, logout to Local Play, and a PINE Hoven Gorge
start/reload with working HUD, minimap, controls and Mod Menu. In Local Play,
Map Loader reclaims only signature-matched Horizon map hooks; unknown patch builds remain untouched.
Online gameplay and other module combinations have not all been verified.

## Custom maps

Run either included Horizon updater from this folder, alongside `dl/`:

- Windows: `powershell -File .\horizon_windows_update_cmaps.ps1`
- Bash: `bash ./horizon_unix_update_cmaps.sh`

Choose Deadlocked NTSC. The scripts may also offer other games/regions; those
choices do not add multiplayer support to this loader. Maps are downloaded
separately and are not bundled in the ZIP.

Map packs belong directly in `dl/`. Map Loader scans on the first Local Play
staging visit and retains its catalog across games. Press Square on Custom Maps,
use Forge's Refresh Custom Maps, or send a PINE refresh after changing files.
No `.idx` files are needed. Keep `dl/mods/utils/mapscan.irx` installed.
Maps requiring Horizon-specific custom code/game modes are unsupported.

## Optional debug handler and expanded RAM

Copy `dl/mods/utils/exceptionhandler.bin` to `dl/mods/debug/exceptionhandler.bin`
to enable the handler; create `debug/` if needed. Remove that copy to disable it.
Restart after either change. It reserves memory at `0x98000`.

Expanded-RAM support needs a compatible BIOS. If BIOS Warning appears, disable
128 MiB EE RAM or select a compatible BIOS and restart. Dismissing the warning
does not change emulator settings.

See the [repository](https://github.com/Metroynome/dl-mod-loader),
[Forge guide](https://github.com/Metroynome/dl-mod-loader/blob/master/modules/forge/README.md),
and [PINE interface](https://github.com/Metroynome/dl-mod-loader/blob/master/modules/forge/PINE.md).
