# Login and character selection profile

The separate `selection` build adds a narrow route to login, the selector and
creation without changing the boot-only control. The sources are the
[audited GameServer](01-gameserver-boot.md) and the research checkout,
`mydll-gameserver/decompilacion-completa/reconstruction/cleanroom_t9/`:

- `contracts/LOGIN_ONLY.md` defines the bounded admission and `F3/00`/`0D`
  transforms tested with synthetic buffers. `INSTALLED_MYDLL_LOGIN_STAGE1.md`
  describes the earlier build that reached the selector with a real client.
- `tools/vm2003_stack/ADDSTAT_AB_MATRIX.md` records the corrected WORLD build
  `a2c4eb5469ac25096dd63506c21a25ebd7d8b3fb9a662c58d41b4011c4a8292f`: MG
  succeeded with a level-220 fixture, failed with level-1 fixtures, and DL
  succeeded without raising a level in that test. The sequence is evidence
  for that build and fixture, not a new class eligibility rule.
- `contracts/INSTALLED_PACKET_CRYPTO.md` and `static/asm/0x1002c651.asm`
  establish the output XOR. The [original-stack baseline](03-character-create-baseline.md)
  is a separate Dark Lord observation and did not load a MuRet DLL.

## Packet contracts

| Boundary | Admitted scope |
| --- | --- |
| `f28` RX → native `0x00414180` | Bounded shapes for `03`, `0E`, `F1/01`, `F1/02`, `F1/03`, `F3/00`, `F3/7A` and plain `F3/01`. `F3/00` and `F3/01` require the native selector state. No map join (`F3/03`) or deletion (`F3/02`). |
| `f34` TX → native `0x00440240` | Stock client-check, keepalive, account greeting/result/close; adapted `0D`, `F3/00` and correlated `F3/01`. The notice adds nine zero bytes after the four-byte prefix. |
| `f60` | XOR `0xAA` over the buffer supplied by native send; no extra XOR at `f34` or on receive. Native C3/C4 parsing and encryption are not replaced. No `CCrypt` binding. |

`RecvDataParse` at `0x0043fa07` can leave the upper bytes of the opcode argument
untouched. Compare and delegate the low byte. For normalized C3 the supplied
length is `n`, the C1 header says `n+2`, and only `n+1` bytes are backed by the
serialized payload. The plugin does not change the packet, encryption flag or
serial on delegation; the GameServer still runs its checks and owns authentication.

`CGPCharacterCreate` at `0x00417420` and the DataServer own name, slot and class
eligibility. The 15-byte request reaches stock unchanged, including MG selector
`0x30` and DL selector `0x40`. Only the correlated 40-byte reply at the send
callback expands to 44 bytes: level `1`, client class `0x60` or `0x80`
respectively, and the original result. Failed creation uses `0xFF` in the
equipment region rather than emitting uninitialized bytes from stock
`JGCharacterCreateFailSend` (`0x00410a50`). One request at a time is tracked
per slot; a new native greeting clears pending state. Do not interpret
`DELEGATED_TO_GS` as success.

## List bridge

At `0x0040e815` the GS supplies `f0c` the DataServer list packet. The
`C2/01` header is 24 bytes with up to five 78-byte rows. Keep the header
and compact each five-byte item to the three bytes expected by native
`JGPGetCharList` at `0x0040f6c0` (54-byte rows). This preserves the
native account check and the `Magumsa` byte. Only during that synchronous
native call is the richer equipment view available to `f34`, on the same
thread and for the same slot. The `F3/00` reply expands from `5 + 26*n`
to `7 + 34*n`. The advertised unlock count uses `[Common] Lv01` and
`Lv02` from `mx_config.ini` beside the executable (defaults `220` and `300`);
those numbers do not override the stock creation guards.

## Local verification

VC6/Wine fixtures cover the per-slot request/reply, rejection equipment,
native-call arguments, charlist scope, boot callback publication, observer
decisions and output XOR. The DLL is PE32 x86, imports only `KERNEL32.dll`
and exports only `Init @1`. An offline differential run matched the prior
charlist translator on 5,000 generated inputs. These checks did not run
the GameServer or client.

### Native byte comparison

The [native fidelity table](coverage/port-native-fidelity.tsv) records only
`f60` and `f48`. The original `910966f7…70fa1` and the installed reference
`8b39364e…16e9` agree byte for byte in both bodies, and both bodies agree with
the contiguous archived assembly. These checks do not cover the mixed receive
dispatcher or imply that the compatibility adapters are exact ports.

- **`f60` (`0x1002c651`): exact.** The linked selection DLL now publishes the
  VC6 `__stdcall` implementation directly. All 56 bytes match the original,
  including the signed length check and `ret 8`. For a positive length the
  caller supplies a writable buffer; the GS passes a pointer into its native
  send buffer at `0x004401cc` and `0x00440828`. A zero or negative length
  touches no bytes. The former wrapper and null guard were not in the original.
- **`f48` (`0x10028580`): deliberate difference.** All 649 bytes match between
  the references. MuRet differs at callback offset `0x05`: the original
  reserves `0x30` on the stack and MuRet reserves `0x48` in the checked build.
  The original fills its local path and loops directly over the map ranges;
  MuRet resolves the path at boot, preflights files, checks the resulting
  map state, and writes a marker. The GS loader target, argument order,
  `this` in `ECX`, callee cleanup, and all 29 map/terrain pairs match the
  reference contract in the offline fixtures. Those checks are not native
  map-load acceptance. If the checked build cannot complete terrain loading
  or publish its map marker, it exits the process; the native trial below
  did not exercise that failure path.

The selection build emits `build/mydll-vc6/selection/MyDll.map`. To check the
linked bytes with the offline `pefile` dependency used by the [census](05-original-function-census.md),
run this from the repository root:

```sh
python3 -B Server/MyDll/tools/verify_selection_native.py \
  "$ORIGINAL_MYDLL" "$INSTALLED_MYDLL" \
  build/mydll-vc6/selection/MyDll.dll build/mydll-vc6/selection/MyDll.map
```

This gate rejects the former `f60` body and a DLL paired with another build's
map. It reports the `f48` prologue difference rather than treating it as an
exact port. The seven-slot closed profile is unchanged by this comparison.

Defer mixed and virtualized bodies to a separate investigation. The
[MuYes/RVD 1.01e reference](01-gameserver-boot.md#artifact-and-evidence) can
suggest observable behavior to test, but similarity does not establish
authorship, identical behavior, or equivalence for any 1.02i region.

## Windows Server 2003 R2 attempt

The `0386245c…6d58` selection artifact ran from a new `GameServer_MuRet_Selection_0386245c`
directory. The audited GameServer and its `mx_config.ini` matched the original
copies by read-back. The six backend listeners retained their PIDs through
the switch, with no connected client. The alternate GS published
`MYDLL_BOOT_ACTIVE PID=632 START=01dd7d641bd2577e RELOC=2814 SLOTS=7`.
It did not open `55901`. Its observer recorded 15 blocked `C1/9B` TX attempts
of 14 bytes and no RX. No login or character action ran.

The Windows dialog reports a read failure at `0x00497927` on address `0x00004c0d`.
The screenshot SHA-256 is `4df2ea6e6d208ac4e7b18bdd2d2fd6a6e7cfd1ad2863cf70e7c1583813de4389`.
In the target ASM that instruction is the first terrain byte read in
`BlockCastleDoor`, after loading the terrain pointer for map `11 + bridgeIndex`.
The routine accepts bridge indices 0 through 6, but the unset `f48` leaves the
fallback map loop at `0x0048a669`, which stops **before** map 17. The preceding
research candidate's `f48` loads indices 11 through 17 from `Terrain12.att`.
The map-count mismatch is the leading explanation; this run did not capture
registers and therefore did not directly identify the faulting map row.
The same fifteen TX rows appeared in the earlier boot-only probe, which did
not capture this dialog. Do not adjust login admission or treat the rows as a
cause of the crash.

## Terrain correction

The [repack reimplementation PR #38](https://github.com/EmaNLJN/mu-server-repack-research-102i/pull/38),
head `484ea5c9`, publishes `f48` and loads map 17 with `Terrain12.att`.
Its `features/host/mx_boot_hooks_bodies.inc` is the source for the selection
profile's load order: maps 0–10 with terrains 1–11; maps 11–17 with terrain
12; maps 18–23 with terrain 19; and maps 24, 30, 31, 33, 34 with terrains
25, 31, 32, 34, 35 respectively. The host selector calls `f48` with no
arguments and jumps to `0x0048a6b6` after it returns, so a callback cannot
load only map 17 and rely on stock to load the rest. The omitted stock
`ItemInit` has an empty body at `0x00444390`.

The correction resolves `..\Data` relative to the executable, checks that
the named files exist, and calls native `LoadMapAttr` via `0x00401fc3`.
It checks the terrain pointer and map number after each native call.
The boot marker now shows eight slots and a separate map marker counts
successful loads. The seven-slot artifact stays available as a control.
VC6/Wine fixtures check the complete map/terrain order, the map-17 failure
boundary, the `thiscall` stack and eight-slot publication. These fixtures
do not establish that later startup or client login works.

## Native boot result: selection `9dd9dd10`

On 2026-11-26 the selection DLL built with VC6 under Wine ran natively on
Windows Server 2003 R2 from a new `GameServer_MuRet_Selection_9dd9dd10`
directory. Read-back before startup verified:

| File | SHA-256 |
| --- | --- |
| GameServer | `b2a4521bd7969c61296531551df31e8741cea8235cf5865702f83c51d52a7d5e` |
| Selection MyDll | `9dd9dd1054c4637c36668dd2c233f6f632e74699a62f76927c6ad050d4e83dde` |
| `mx_config.ini` | `75986d3aa9a932aa65c53bc81dc3cae1d6d51c88415fc4d5d8dc7f54152fd1f9` |
| `debug_mydll.ini` | `3783847b608badbbd281de8958eb8e31c187c72d0bfa429e2057b0d1b8767b1e` |

```text
MYDLL_BOOT_ACTIVE PID=1824 START=01dd7d6f7baf7cac RELOC=2814 SLOTS=8
MYDLL_MAPS_LOADED COUNT=29
```

The early and +35-second WMI samples identified the same executable, PID
and creation time. The creation time matches the boot marker's `FILETIME`
at WMI's precision. The stock console reported LoginServer and both
DataServer connections and creation of port `55901`; an independent
`netstat` sample showed `55901` listening under PID `1824`, with all
baseline backend listeners and PIDs unchanged. The +35-second capture
showed no error dialog. Both the candidate and the restored original
printed nonfatal monster-position diagnostics. This does not establish
world-data parity.

The observer recorded 15 blocked `C1/9B` TX attempts of 14 bytes and zero RX
rows. No client connected. The candidate stopped without force and the
original then listened on `55901` under PID `1776`; the GS, original DLL,
and `mx_config.ini` were reverified by read-back. No backends were restarted.

The maps receipt SHA-256 is
`604a4435053abf16d83bb953a9cfa50bdca76f1c78a005c49213fa1ce77ff0ca`.
The +35-second screenshot SHA-256 is
`039d84bab7a6eb276cb50c6b0caef762c815b21d0f779701af0b03fd3bae0748`.
The boot result covers this selection DLL only, not the seven-slot closed
control, a build produced inside the VM, or successful login or creation.

## Native acceptance

Run only in an isolated test copy with one GS and verified, healthy toy
backends. Record GS, DLL and INI hashes plus process identity. Require that
the same process passes the native map-load convergence and survives 30 seconds
without an error dialog, and verify the listener and backend dependencies
before attempting client login. If the candidate does not pass, capture the
exit cause and compare against a boot control before opening client access.

With the real client, verify a login and selector, a rejected MG attempt on
an ineligible fixture, and successful MG and DL creation on eligible
fixtures with room. Reconnect and confirm both creations appear; do not enter
the map. Compare to the original on equivalent fixtures, without modifying
SQL, credentials, the original DLL or player state outside these actions.
Keep observation to header metadata and report boot, login, creation and
persistence as separate results.
