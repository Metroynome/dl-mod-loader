# Cooperative moby scan coverage

These are observations of the instantiated moby pool, including dormant and disabled slots. They are not proof that every challenge or dynamically spawned class was exercised. Unknown names remain placeholders; presence never changes an implementation status.

## Catacrom

The clean NTSC-U Catacrom co-op dump was scanned across its full pool when creating the initial checklist. Subsequent Blade Ball gameplay was tested. Other challenges, vehicle missions and transient effects have not been exhaustively inventoried.

## Sarathos — initial live pool, 2026-11-22

NTSC-U host, Cooperative level 24. Pool 0x01bfda80..0x01c39f80: 965 slots; 58 distinct nonzero oClasses. Reads were chunked and checked for truncation.

New classes added to both library enums and the checklist: `0x0c26`, `0x2146`, `0x2426`, `0x2461`, `0x24c8`, `0x24f3`. Their identities remain unverified.

Hoverbike `0x2054` is already listed but was absent from this instantiated pool. Scan its challenge separately, including riding/firing and mission-specific spawns. Also check other unique encounters or vehicle challenges before declaring full level coverage.

### Observed classes

| oClass | Instances | Active (mode/state check) |
| --- | ---: | ---: |
| 0x01f5 | 9 | 9 |
| 0x01f7 | 1 | 0 |
| 0x01ff | 7 | 7 |
| 0x0326 | 9 | 9 |
| 0x03ef | 5 | 0 |
| 0x0b37 | 1 | 1 |
| 0x0c26 | 1 | 1 |
| 0x0c60 | 1 | 0 |
| 0x0ce9 | 4 | 3 |
| 0x0f6e | 2 | 2 |
| 0x1087 | 1 | 1 |
| 0x1096 | 1 | 1 |
| 0x1768 | 9 | 4 |
| 0x19b0 | 8 | 8 |
| 0x2011 | 1 | 0 |
| 0x2022 | 1 | 1 |
| 0x2036 | 1 | 1 |
| 0x2037 | 5 | 5 |
| 0x2039 | 2 | 0 |
| 0x2051 | 20 | 0 |
| 0x205c | 6 | 0 |
| 0x2077 | 2 | 2 |
| 0x2081 | 3 | 3 |
| 0x208a | 2 | 1 |
| 0x2090 | 6 | 6 |
| 0x20a3 | 1 | 1 |
| 0x20c4 | 15 | 5 |
| 0x2112 | 23 | 23 |
| 0x2115 | 8 | 8 |
| 0x211e | 1 | 1 |
| 0x211f | 2 | 2 |
| 0x2130 | 24 | 0 |
| 0x2133 | 24 | 24 |
| 0x2146 | 1 | 1 |
| 0x23f7 | 2 | 2 |
| 0x23fd | 2 | 1 |
| 0x23ff | 71 | 16 |
| 0x2401 | 2 | 2 |
| 0x2419 | 15 | 15 |
| 0x2426 | 4 | 4 |
| 0x2432 | 1 | 1 |
| 0x2437 | 10 | 9 |
| 0x2461 | 2 | 2 |
| 0x246b | 2 | 2 |
| 0x247a | 2 | 2 |
| 0x247b | 1 | 1 |
| 0x24c8 | 150 | 150 |
| 0x24f3 | 4 | 0 |
| 0x2546 | 9 | 9 |
| 0x262d | 4 | 4 |
| 0x2639 | 1 | 1 |
| 0x263a | 1 | 1 |
| 0x265a | 4 | 4 |
| 0x2664 | 35 | 35 |
| 0x2682 | 13 | 0 |
| 0x268b | 2 | 0 |
| 0x26be | 1 | 1 |
| 0x26c2 | 1 | 1 |

## Sarathos — hoverbike challenge, 2026-11-22

Scanned 965 slots and 52 distinct oClasses. Confirmed two instantiated Hoverbikes (`0x2054`), Vehicle Pad (`0x1981`), Sarathos bridge components (`0x20e8`, `0x20e9`, `0x20ea`) and 86 vehicle-mission land mines (`0x243c`). Classes `0x2116` and `0x2117` are new and remain unidentified; added to both library enums and the checklist. The two Sarathos captures now cover 67 distinct classes in total. Dynamic riding/firing/destruction effects still need observation.

### Observed classes

| oClass | Instances | Active (mode/state check) |
| --- | ---: | ---: |
| 0x01f5 | 2 | 2 |
| 0x01f7 | 1 | 0 |
| 0x01ff | 1 | 1 |
| 0x0326 | 2 | 2 |
| 0x03ef | 4 | 0 |
| 0x0b37 | 1 | 1 |
| 0x0c60 | 1 | 0 |
| 0x0ce9 | 4 | 3 |
| 0x0f6e | 1 | 1 |
| 0x1087 | 1 | 0 |
| 0x1096 | 1 | 0 |
| 0x1768 | 9 | 4 |
| 0x1981 | 1 | 1 |
| 0x19b0 | 8 | 8 |
| 0x2011 | 1 | 0 |
| 0x2022 | 1 | 1 |
| 0x2036 | 1 | 1 |
| 0x2039 | 2 | 0 |
| 0x2051 | 20 | 0 |
| 0x2054 | 2 | 2 |
| 0x205c | 6 | 0 |
| 0x208a | 1 | 1 |
| 0x20a3 | 1 | 1 |
| 0x20c4 | 10 | 0 |
| 0x20e8 | 8 | 8 |
| 0x20e9 | 8 | 8 |
| 0x20ea | 8 | 8 |
| 0x2115 | 1 | 1 |
| 0x2116 | 1 | 1 |
| 0x2117 | 34 | 34 |
| 0x211e | 1 | 1 |
| 0x211f | 2 | 2 |
| 0x2130 | 24 | 0 |
| 0x2133 | 24 | 24 |
| 0x23f7 | 2 | 2 |
| 0x23fa | 2 | 0 |
| 0x23fd | 1 | 0 |
| 0x23ff | 64 | 4 |
| 0x2419 | 15 | 15 |
| 0x2432 | 1 | 1 |
| 0x2437 | 1 | 0 |
| 0x243c | 86 | 86 |
| 0x246b | 2 | 2 |
| 0x247b | 1 | 1 |
| 0x24c8 | 150 | 150 |
| 0x2546 | 9 | 9 |
| 0x262d | 4 | 4 |
| 0x265a | 4 | 4 |
| 0x2664 | 35 | 35 |
| 0x2682 | 13 | 0 |
| 0x26be | 1 | 1 |
| 0x26c2 | 1 | 1 |

## Sarathos — after riding, collecting a ring and firing/explosion

Observed 55 classes in 965 slots. Newly observed since both earlier scans: `0x0d4e`, `0x1a50`, `0x1b35`, `0x1f8c`, `0x20b8`. Known rock/smoke plume `0x1a50` and vehicle shot `0x1b35` appeared. Added previously unlisted `0x0d4e`, `0x1f8c` and `0x20b8`; only 0x1f8c is identified from native spawn code. Combined Sarathos coverage is now 72 classes. This post-action snapshot is not exhaustive coverage of transient effects.
