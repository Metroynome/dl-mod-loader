# Online transport prototype

Optional NTSC-U campaign module. It establishes a direct UDP connection between
one host and one guest, and carries small adapter messages. It does **not** by
itself synchronize players, cameras, enemies, missions or saves. No external
server application is needed. The optional [Co-op adapter](../coop/README.md)
adds the first controller-input experiment.

Build with the normal Docker/Make setup:

```sh
make NAME=online REGION=ntscu
```

Copy `hostfs/dl/mods/online.ntscu.bin` and the entire generated
`hostfs/dl/mods/utils/online/` directory to the matching hostfs paths. Add
`online campaign` to `dl/config/modules.txt` and restart. The normal development
list leaves it disabled. Mod Menu and expanded RAM are not required.

Load a campaign/Cooperative save first. In MOD MENU → Online:

- On the host, select Host. It listens on Host Port (default `25565`).
- On the guest, enter the host's four IPv4 bytes and Host Port, then select Join.
- Host returns to gameplay once listening; Join returns once connected. Failed
  requests leave the menu open for correction or retry.
- Defaults are host IP `192.168.1.190`, Host Port `25565`, and guest Local Port
  `25566`. These let the two test emulators connect on the same PC unchanged.
- Hold Left/Right on numeric fields to repeat. D-pad pressure increases the
  repeat speed, and long holds accelerate large numbers such as ports.
- Connected means the handshake and peer heartbeats work. Disconnect closes the
  socket; it does not exit campaign. After a failed connection, select Join or
  Host directly to retry. Driver/RPC startup failures still require a cold restart.
- If the guest disconnects or times out, the host keeps listening. The same
  guest endpoint can reconnect immediately; a different endpoint waits for the
  previous connection to time out. Disconnect on the host stops hosting.

With PCSX2's Sockets backend, enter the host PC's LAN IP, not its emulated
`192.0.2.100` address. Hosting sends a small, ignored LAN probe periodically so
that the socket backend opens and keeps its incoming UDP mapping. On physical
consoles use the console's LAN address. Internet access/port forwarding and
physical PS2 hardware are not verified; this prototype is for LAN testing.

## Discovery and requests

Follow the loader discovery pointer at `0x000a0000` to its module table, find
`online.ntscu.bin`, then read the exports pointer at that module's load address.
It points to `onlineControl_t` in [include/online.h](include/online.h).
Service `ONLINE_SERVICE_ID`, version `2`, publishes `onlineApi_t` instead. Its
`control` pointer references that mailbox. This interface is experimental.

Require magic `0x314e4c44`, size `0x34`, and an advancing heartbeat. Write the
command at `+0x24`, local port at `+0x28`, host port at `+0x2a`, and the four host
IPv4 bytes at `+0x2c`. Submit a fresh nonzero request ID at `+0x20` last.

Commands: `1` Host, `2` Join, `3` Disconnect. `acceptedId` at `+0x14` acknowledges
processing. Read `status` at `+0x0c` separately: `0` Idle, `1` Loading drivers,
`2` Obtaining IP, `3` Hosting, `4` Connecting, `5` Connected, `6` Failed.
Received/sent handshake counters are at `+0x18`/`+0x1c`; the assigned local IP is
at `+0x30`. Sent counts attempts, not delivery acknowledgements.

Errors: `-1` unavailable context, `-2` busy/invalid endpoint, `-3` unknown command,
`-4` socket/RPC failure, `-5` driver/bind failure, `-6` address/peer timeout.
The emulator log identifies which driver failed.

## Adapter messages

Call `onlineFind(loader)` and check `role()` before exchanging snapshots. `send`
queues up to 224 bytes tagged with a channel ID. Newer input replaces an
unsent message on the same channel; another channel waits until it is sent. `receive` consumes the newest message for that channel and returns
its length. Wrong-peer, wrong-session and reordered messages are discarded.
An adapter may multiplex channels. The transport does not provide reliable
event delivery; Co-op adds acknowledgements for object batches. Wire protocol
revision 3 requires matching builds on both
systems. PINE mailbox offsets are unchanged.

## Implementation limits

The EE module loads standard PS2SDK drivers and a small IOP UDP helper on demand,
using a 4 KiB streaming buffer. It never resets the IOP or unloads existing game
drivers. RPC and socket polling do not wait for remote packets. Driver loading
itself uses the resident synchronous loader and may briefly pause a frame.

Driver/RPC failures quarantine pending buffers rather than reusing in-flight DMA.
Loaded IOP drivers remain resident. Cold-restart before switching to native
Online Play; travel/IOP resets, other regions and host migration are unverified.
Session tokens reject stale/wrong-peer packets; they provide no authentication
or encryption. Reliable game events and world authority remain separate work.

The bundled `ps2dev9`, `netman`, `smap` and `ps2ip-nm` binaries come from the
existing PS2SDK build environment. Their license is copied alongside them as
`PS2SDK-LICENSE.txt`. Source: https://github.com/ps2dev/ps2sdk .
