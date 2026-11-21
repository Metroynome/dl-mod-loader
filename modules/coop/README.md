# Cooperative input experiment

Optional NTSC-U Deadlocked adapter for the normal-control area of Cooperative
DreadZone Station (level 21). Tutorial playback, other maps and other regions
remain unsupported. The variable-address tables deliberately contain only the
verified Station addresses; they use the full layout supported by the pinned
librac4 build.

Build `make NAME=online REGION=ntscu` and `make NAME=coop REGION=ntscu`. Install
the Online module and its driver directory as described in
[Online](../online/README.md), then copy `coop.ntscu.bin` to `dl/mods/` and enable
`coop campaign` in `dl/config/modules.txt`. Both modules are disabled in the
normal development list. Neither needs expanded RAM or Mod Menu.

Load the same Cooperative save and mission on both systems, connect using
Online, then enable **Co-op → Co-op Input Test** on both. This toggle starts OFF
and is not saved. Without Mod Menu, follow the module discovery pointer to the
Co-op module's exports and set only `coopControl_t.enabled` to `1`.

- Host controller 1 controls player 1 locally and sends that input to the guest.
- Guest controller 1 controls player 2 locally and sends that input to the host.
- Start, Select, L3 and R3 stay local so remote input does not open menus.
- Missing/old input becomes neutral after roughly a quarter second of EE time.
- Turning the test off/disconnecting restores native controller ownership.

The adapter chains the native raw-input callbacks and defers to Mod Menu's input
capture. It verifies the overlay, two-player state, hero lookup and current pad
ownership before attaching. Unsupported states release its callbacks.

This is **input replication only**. Each game still simulates its own world, and
both screens remain native split-screen. Starting positions, enemy state,
damage, objectives, saves and checkpoints are not synchronized or corrected.
Do not use it as a complete co-op implementation. Disconnect/disable the test
before travel; level changes and native Online Play are not verified.

For the isolated emulator test setup, the guest's physical SDL controller input
is disabled. Keyboard mappings are W/A/S/D movement, I/J/K/L camera, X jump,
Z wrench, Enter Start and Backspace Triangle; normal PCSX2 settings are untouched.
