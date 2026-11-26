# Character creation: original-stack baseline

On 2026-11-26 the operator connected from VMXP on the ThinkPad via the existing
`mu-lan` tunnel, used the original laboratory GS/MyDll, and reported successful
creation of one Dark Lord. The GS PID was `2312` in the pre-run inventory.
No GS, backend, DLL or tunnel changes were made for this run.

## Observation

After the operator connected, `ss -Htin` sampled one established GameServer TCP
flow on the host-facing side of the tunnel for 90 seconds. It saved only cumulative
byte and data-segment counters, direction and elapsed time. It did not capture
bytes from any packet or from the login.

| Elapsed | RX bytes | TX bytes | RX data segments | TX data segments |
| ---: | ---: | ---: | ---: | ---: |
| 10,648 ms | 216 | 164 | 8 | 4 |
| 26,709 ms | 231 | 208 | 9 | 5 |
| Delta | **15** | **44** | **1** | **1** |

This was the only TX change. The other RX changes in the window were 24 bytes
each, with no concurrent TX change at the sampling resolution. The counter log
SHA-256 is `81a3efdb50cbaec93cf00171b086c39112f55c6c132c1b2170a007b86fb7dc93`.
The operator reported creation; the test did not verify database persistence,
character selection after reconnect, or entry into the map.

## Static correlation, not a wire decode

In the [research checkout](01-gameserver-boot.md), the reconstructed GS has
`PMSG_CHARCREATE` (`F3/01`, 15 bytes) in `gameserver/src/reconstructed/win32-vc6/gs_protocol.h`
and the dispatcher reaches `CGPCharacterCreate` at `0x00417420`. The stock
DataServer reply handler at `0x00410920` builds a 40-byte `F3/01` response.
The historical MyDll static triage (`reconstruction/analysis/triage/bloque_0.md`
and `bloque_1.md`) describes a C2S class-state adapter at `0x10005778` and an
S2C converter at `0x100096df` that expands the reply from 40 to 44 bytes.

The measured `15 RX / 44 TX` pair is consistent with that route, but TCP counters
do not identify an opcode, class byte, function, or individual MU frame. The
Dark Lord type and success came from the operator's UI observation. This is a
baseline for a future implementation once MuRet can admit login and character
selection, not evidence that its stub supports `F3/01`.
