# Startup menu tab crash

Investigated on CUSA00190 version 01.81, 18 October 2026.

Switching away from Campaign/About lazily initializes Killzone's Turbine client.
The immediate failure in the launcher logs was an unsupported `sceRudpInit`
import, called from `FUN_456dd0` (return address `0x456dff`). This was a runtime
compatibility failure, rather than a rendering fault.

Scripted reproduction then exposed two additional requirements:

- `FUN_1e3de0` / `FUN_1e3fb0` create a local signaling socket even while PSN is
  signed out. Refusing socket creation/binding leaves the client null; its
  workers assert in `TurbineClientSocketSelect` (`FUN_1eda20`). The offline
  provider now tracks local socket handles, initializes bind/name metadata and
  allows local configuration/epoll setup. Empty polls wait instead of spinning.
  Connections/sends remain unavailable; reads report would-block. No host
  network socket or external connection is created.
- `FUN_2ad6e0` requires `sceRtcGetWin32FileTime`. The runtime now converts a
  validated calendar value into 100 ns intervals since 1601-01-01, with checked
  pointers and pre-epoch rejection. The game clock/simulation timing is unchanged.

RUDP supports local init/end and an empty event pump, as called by the menu's
update worker. It does not implement peer networking; other unsupported imports
continue to stop explicitly. PSN remains signed out.

Validation: native service/dialog regression checks, socket lifecycle/bounds and
offline-state checks; FILETIME epoch, leap-year, fractional-second and Windows
conversion checks; full build. An isolated game run accepted four R1 presses
followed by four L1 presses, exercised the RUDP initialization path, continued
presenting frames, and emitted no unsupported-import stop, guest assertion,
access fault or Vulkan validation error. The test stopped its own process.

Visual confirmation of the individual tabs and their actions remains pending.
This change does not establish online multiplayer or PSN support.
