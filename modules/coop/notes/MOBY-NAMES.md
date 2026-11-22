# Moby name audit

Audited 2026-11-22 against the library lists, the available prototype decompilation, and the clean NTSC-U Catacrom co-op EE dump. This is an evidence-based correction pass, not a claim that all labels are verified. No runtime behavior, oClass values or implementation statuses were changed.

## Sources and method

- `game_dl/projectgui/menu/menustats.cpp`, `MenuStats_OClassTagLookup`: direct oClass-to-message-tag mapping.
- Retail dump `ntscu.22.Catacrom.bin`: HelpMsg records are 16 bytes, with text pointer at 0x00 and tag at 0x04. The matching English enemy labels occupy entries 0x01b09600 onward. Tags and pointed-to strings were checked together; names were not inferred from color.
- `game_dl/tables.cpp`, `TBL_EnemySkinDefTable`: paired legs and torso variants map to their base Striker classes. Torso variant names follow those verified pairs; the Stats menu counts the legs actor.
- `game_dl/globals.cpp`, `GetMobySkinBase`: Qwark torso/legs map to the regular or Ghost Striker pair depending on level.
- Native spawn/helper code listed below identifies effects and helper classes. Several decompilation bodies are incomplete, so a descriptive function name alone is not enough to identify a different class. Explicit CreateMoby IDs and call sites were checked for the renamed helpers.
- Online references were searched, including [MobyViewer](https://github.com/CreepNT/MobyViewer). Display names found online do not establish an oClass mapping by themselves. The verified changes below rely on local game evidence.

## Confirmed corrections and additions

Both library headers use the same canonical names and values. Previous identifiers remain enum aliases for source compatibility; they are historical labels, not additional classes. CSV rows use one canonical display name per oClass. Component IDs remain identified as components even when Stats displays the whole enemy name.

| oClass | CSV name | Canonical identifier | Evidence |
| --- | --- | --- | --- |
| 0x03ef | Camera Collision / Reserved Weapon Helper | `MOBY_ID_CAMERA_COLLISION_HELPER` | camera.cpp: Camera_handleCollWithHero; update/moby1007.cpp: CreateMoby(0x3ef); weapons.cpp: WPN_DeleteMoby |
| 0x1a50 | Rock / Smoke Plume | `MOBY_ID_ROCK_SMOKE_PLUME` | update/moby6736.cpp: Spawn_Moby6736 and smoke fields; moby6736.h: smoking/non-smoking types |
| 0x2008 | Beam Turret | `MOBY_ID_BEAM_TURRET` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3219 |
| 0x2039 | 70 Millimeter Stalker Turret | `MOBY_ID_STALKER_TURRET_70MM` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x321d (Landstalker component) |
| 0x2051 | Tri-spike Swarmer | `MOBY_ID_TRI_SPIKE_SWARMER` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x320c |
| 0x205c | DreadZone Dropship | `MOBY_ID_DREADZONE_DROPSHIP` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x321e |
| 0x20c4 | Leviathan (Tail / Stats Actor) | `MOBY_ID_LEVIATHAN_TAIL` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x321f; retain existing part identifier |
| 0x23ff | Shrieker | `MOBY_ID_SHRIEKER` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3210 |
| 0x242c | Pincer Ray | `MOBY_ID_PINCER_RAY` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x320f |
| 0x2463 | Ghost Robot Zombie | `MOBY_ID_GHOST_ROBOT_ZOMBIE` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x362b |
| 0x2468 | DZ Ghost Executioner | `MOBY_ID_GHOST_EXECUTIONER` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x362a |
| 0x246a | DZ Ghost Striker Torso | `MOBY_ID_GHOST_DZ_STRIKER_TORSO` | globals.cpp: GetMobySkinBase pairs 0x2726/0x2727 with 0x246a/0x246d; stats tag 0x3629 |
| 0x246d | DZ Ghost Striker Legs | `MOBY_ID_GHOST_DZ_STRIKER_LEGS` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3629 |
| 0x2473 | Pincer Ray Variant 2473 | `MOBY_ID_PINCER_RAY_VARIANT_2473` | menustats.cpp: MenuStats_AddKillCount merges 0x2473 into 0x242c; exact variant name unconfirmed |
| 0x24d3 | Tremor | `MOBY_ID_TREMOR` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x32b8 |
| 0x24f9 | Laser Back | `MOBY_ID_LASER_BACK` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x32bc |
| 0x2570 | Reaper | `MOBY_ID_REAPER` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3220 |
| 0x2695 | Claw Tooth Swarmer | `MOBY_ID_CLAW_TOOTH_SWARMER` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x320d |
| 0x26ce | DZ Berserker Torso | `MOBY_ID_DZ_BERSERKER_TORSO` | tables.cpp: TBL_EnemySkinDefTable; stats mapping for the matching legs; 0x26ce -> base torso 0x2133 |
| 0x26cf | DZ Berserker Legs | `MOBY_ID_DZ_BERSERKER_LEGS` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3212 |
| 0x26d0 | DZ Viper Torso | `MOBY_ID_DZ_VIPER_TORSO` | tables.cpp: TBL_EnemySkinDefTable; stats mapping for the matching legs; 0x26d0 -> base torso 0x2133 |
| 0x26d1 | DZ Viper Legs | `MOBY_ID_DZ_VIPER_LEGS` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3213 |
| 0x26d3 | DZ Stinger Torso | `MOBY_ID_DZ_STINGER_TORSO` | tables.cpp: TBL_EnemySkinDefTable; stats mapping for the matching legs; 0x26d3 -> base torso 0x2133 |
| 0x26d4 | DZ Stinger Legs | `MOBY_ID_DZ_STINGER_LEGS` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3214 |
| 0x26d6 | DZ Annihilator Torso | `MOBY_ID_DZ_ANNIHILATOR_TORSO` | tables.cpp: TBL_EnemySkinDefTable; stats mapping for the matching legs; 0x26d6 -> base torso 0x2133 |
| 0x26d7 | DZ Annihilator Legs | `MOBY_ID_DZ_ANNIHILATOR_LEGS` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3215 |
| 0x26e0 | Rhyno Swarmer | `MOBY_ID_RHYNO_SWARMER` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x320e |
| 0x26e1 | Overlord | `MOBY_ID_OVERLORD` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x3217 |
| 0x2722 | Landstalker (Base / Stats Actor) | `MOBY_ID_LANDSTALKER_BASE` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x32b7; preserve component distinction |
| 0x2730 | Missile Turret | `MOBY_ID_MISSILE_TURRET` | projectgui/menu/menustats.cpp: MenuStats_OClassTagLookup; retail HelpMsg text; tag 0x321b |
| 0x2758 | Swarmer Variant 2758 | `MOBY_ID_SWARMER_VARIANT_2758` | menustats.cpp: MenuStats_AddKillCount merges 0x2758 into 0x2051; exact variant name unconfirmed |
| 0x027f | Point Light Effect | `MOBY_ID_POINT_LIGHT_EFFECT` | update/moby639.cpp: CreateMoby(0x27f), pointlight/radius/color fields |
| 0x13a1 | Explosion Effect | `MOBY_ID_EXPLOSION_EFFECT` | update/moby5025.cpp: SpawnMoby_5025 creates 0x13a1 and explosion/damage/FX components |

## Retail enemy text

These strings were read from the retail dump rather than copied from an enemy guide. Plural labels are made singular in the checklist.

- Message 0x320c: Tri-spike Swarmers
- Message 0x320d: Claw Tooth Swarmers
- Message 0x320e: Rhyno Swarmers
- Message 0x320f: Pincer Rays
- Message 0x3210: Shriekers
- Message 0x3211: DZ Strikers
- Message 0x3212: DZ Berserkers
- Message 0x3213: DZ Vipers
- Message 0x3214: DZ Stingers
- Message 0x3215: DZ Annihilators
- Message 0x3216: Executioners
- Message 0x3217: Overlords
- Message 0x3219: Beam Turrets
- Message 0x321b: Missile Turrets
- Message 0x321d: 70 millimeter Stalker Turrets
- Message 0x321e: Dropships
- Message 0x321f: Leviathans
- Message 0x3220: Reapers
- Message 0x32b7: Landstalkers
- Message 0x32b8: Tremors
- Message 0x32b9: Robot Zombies
- Message 0x32bb: Blade Balls
- Message 0x32bc: Laser Backs
- Message 0x32bd: Cycloids
- Message 0x3629: DZ Ghost Striker
- Message 0x362a: DZ Ghost Executioner
- Message 0x362b: Ghost Zombies

## Unresolved labels

The remaining unknown classes and visual descriptions still need identification. In particular, `BIGASS_ROCK` (0x1a53), `VERY_SMALL_SQUARE` (0x1a2f), `DARK_BLUE_SPIKE_RING` (0x0ceb), its taller variant (0x0cec), `WHITE_ORANGE_FLASH` (0x0cfa), `FLAT_SQUARE` (0x20ba), and the brown-circle classes (0x333d/0x3341) have not been assigned new semantic names without a verified spawn/update path. A simple mesh seen in a viewer can be an effect, placeholder or helper; it is not sufficient evidence to call it scenery. Their existing names are retained provisionally.

The Stats menu merges 0x2473 into Pincer Ray and 0x2758 into Tri-spike Swarmer, but does not label those variants separately. They are explicitly named variants rather than assuming their exact appearance or Ghost behavior.

The `MOON` class (0x0070) is spawned as a colored timed effect by `Spawn_Moby112`, including from explosion code. Its exact visual role remains unresolved; do not infer a literal astronomical moon from the old label. Weapon/model variants and unused legacy classes likewise retain their existing names unless a native use was verified. Category labels are provisional and do not prove a class requires synchronization.

## Additional Sarathos effect

`MOBY_ID_FIRE_PILLAR_EFFECT` (`0x1f8c`): `game_dl/update/moby8076.cpp`, `SpawnMoby_8076`, explicitly creates this class. `initStruct8076` contains sphere/pillar fire colors, pillar direction/speed/density, gravity and damage parameters. CSV name: Fire Pillar / Sphere Effect. Other newly observed classes 0x0d4e and 0x20b8 remain unidentified.
