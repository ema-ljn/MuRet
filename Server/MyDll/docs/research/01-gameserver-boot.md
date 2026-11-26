# GameServer boot contract

## Artifact and evidence

The MU 1.02i repack GameServer used here is PE32 x86, with preferred base `0x00400000`,
image size `0x07ddc000`, and SHA-256
`b2a4521bd7969c61296531551df31e8741cea8235cf5865702f83c51d52a7d5e`.
The addresses below are virtual addresses in that image. The on-disk image is packed, so
the instructions below refer to `gameserver/disasm/gs_oep.asm` in the
[research repository](https://github.com/EmaNLJN/mu-server-repack-research-102i), not the
raw `.exe` bytes at the same virtual addresses.

Use these sources in order; the binaries and local runtime captures are not vendored in MuRet:

1. **Target GS and installed MyDll 1.02i.** The installed DLL is
   `8b39364e6720632b308e48246043668d6a0db0d74f05260763ce8d40919d16e9`. The GS call sites,
   not the DLL pseudocode alone, set the ABI. The static inputs for the relocation
   census are `gs_oep.asm` (`82912f0c…c3d4`) and `mydll_cfgload_tbl.h`
   (`54bfc0fc…fc10`) in the research checkout at `d195d89c`.
2. **Previous own candidate.** The VC6 `CLOSED` run is a positive boot control, not
   evidence that each action it performs is mandatory. Keep its configuration, hooks,
   and liveness evidence distinct from the new stub.
3. **Sister reference: MuYes/RVD 1.01e.** The sibling MyDll is
   `656dc1715d152b34a1a33d4b81f1e292d6d1ea7415457d353e696e312c60c943`;
   its own GS is `799d4dfa16b0b79f55915226252b8232af7af8f4915e10ba9b4b07f05454c024`.
   Static similarities can point to places to check, but do not establish the target
   addresses, defaults, or boot dependencies. Neither sibling binary is a replacement,
   and no conclusion about all of its code follows from the native routines already
   compared.

At `0x0048f730` the host calls through `0x073112ac`, the `Init` cell, then jumps to
`0x0048f740` at `0x0048f736`. There is no check of `EAX`. The call site and cell can exist
without being the current caller; the entry guard needs the actual `0x0048f736` return
address as well as the main image and resolved export. The prior Windows candidate
exported `Init` at ordinal 1 and reached its boot marker. A foreign-host unit test does
not prove this GameServer will reach the marker with the new stub.

## Callback sites

| Slot | GS evidence | Stub contract to verify |
| --- | --- | --- |
| `f08` | `0x0042796f` calls through the slot without a null check. A separate guarded route at `0x00415b7c` pushes two arguments and tests `EAX`, with no visible cleanup before the branch. | No skill handling; verify the stack and neutral result against both call shapes. |
| `f0c` | `0x0040e815` calls the slot with a DWORD argument and does not clean the argument afterwards. | Consume with the correct stack cleanup; do not inspect or convert a character list. |
| `f24` | `0x0045c33a` calls the slot with two DWORDs. A zero result goes directly to the return. | Return zero with the correct stack cleanup; do not create or populate a character. |
| `f28` | Selector `0x00414170` falls through to the native receive body when the slot is null. | Close receive explicitly, with no parsing or forwarding. |
| `f2c` | `0x00468ac9` calls the slot with one DWORD. A nonzero result takes the duel branch. | Return zero with the correct stack cleanup; do not read duel state. |
| `f34` | Selector `0x00440230` falls through to the native send body when the slot is null. | Close send explicitly, with no inspection or forwarding. |
| `f48` | `0x00489fa8` tests the slot. Null resumes the native loader at `0x0048a30e`. | Leave the slot unset. The new stub still needs a real-host boot check on this path. |
| `f60` | `0x004401cc` calls the slot with buffer and length. At `0x00440828` the alternate path can call it even when the preceding slot load yielded zero. | Publish a callable two-argument no-op that does not read or write the buffer. |

The `f08` call at `0x0042796f` pushes five DWORDs. At `0x00427977` the caller adds 16 to
`ESP`, **then** pops `EDI`, `ESI`, and `EBX` at `0x0042797a–7c`, and only at `0x0042797d`
does it reset `ESP` from `EBP`. The callback needs to remove the other four bytes for
the pops to restore the right values. A plain return can pass an end-of-function `ESP`
check while restoring all three registers incorrectly. At the two-DWORD call at
`0x00415b7c`, the continuation either returns or branches to the native body without
cleaning arguments; it needs eight bytes of callee cleanup, with `EAX=0` selecting
the native path. Both are static ABI contracts, not paths observed in the boot run.

Alternate relay locations (`0x00456e84`, `0x0046415f`, `0x00414d1c`, `0x0048fa49`) have
no proven incoming startup path in the previous census. In particular, the last one
is a window-procedure relay; the registered native WndProc bypasses it in the
reconstructed entry (`PRE_INIT_WNDPROC_RELAY.md`). Do not extrapolate the active
`f0c`, `f2c`, `f28`, or `f24` callback contracts to those relays.

## Backing and prior runtime evidence

The previous `CLOSED` candidate applied 2,814 operand relocations: monster definitions
(69), objects (2,550), Blood Castle state (159) and item attributes (36). Comparing
the four tables from `reconstruction/include/mydll_cfgload_tbl.h` with `gs_oep.asm`
found bytes at all 2,814 sites. The operands at `0x0048b9f9`, `0x00405e5b`,
`0x00488d9e` and `0x0047e2f8` contain `0x0cfe394c`, `0x0bdedfcb`, `0x0bdea242`
and `0x08682b48` respectively, all beyond the GS image end `0x081dc000`.
That is evidence against a no-op `Init`; it is not a proof that every relocation is
individually needed before the first liveness check.

The byte-addressed census found 2,814 distinct and non-overlapping four-byte writes.
The six object operands that cross 4 KiB boundaries start at `0x00407fff`, `0x00413ffe`,
`0x00427ffd`, `0x00468ffd`, `0x00470ffe`, and `0x004b2ffe`. The canonical SHA-256 of
the triples `(operand VA, old DWORD, whole-backing offset)`, sorted by VA and packed as
three little-endian DWORDs per row, is
`51997304d4e2ab8f942d9dba2306e737114b1c08b1494abf4241c07bd8e22d8a`.
All 2,814 values in the separate archived `gameserver/disasm/gs_live.asm` agree with
`0x084a14e0 + whole-backing offset`. That base is capture-specific, and neither that
agreement nor the OEP disassembly proves what bytes the next process will present at `Init`.

The native Windows Server 2003 R2 run of the previous VC6 `CLOSED` artifact
(`6cc48cfc…44bb5`, PID 3076) emitted `MX_BOOT_ACTIVE` with 65 config writes, 2,814
relocations and eight slots. The process was still alive at observation; there was no
observed listener on `55901` and no post-map milestone in that receipt. The PE imported
`KERNEL32.dll` and `WS2_32.dll`; its `Init` called `WSAStartup` at
`features/host/mx_boot_init_x86.c:201`. This run kept packet XOR and non-stub bodies.

A separate run of an older `CLOSED` artifact (PID 2200) showed shop/monster loading,
backend connections and a port-creation **console line**, according to
`INSTALLED_MYDLL_BOOT_CANDIDATE.md`; it is not a socket observation from PID 3076.
Neither run proves the need for the 65 CONFIG writes, DLL-side Winsock initialization,
or the custom map loader. The native GS has Winsock setup in `MyWinsockBase.cpp`
(`0x004410f1`) and `WzUdp.cpp` (`0x00442abb`). Its native map loop runs 17 entries
before the common continuation `0x0048a6b6` and `ShopDataLoad` at `0x0048a6c0`.
Use an independently observed stock milestone, not mere liveness or inferred socket
readiness. Verify the no-DLL-Winsock and native-map choices on the isolated target.

Prior evidence: `mydll-gameserver/decompilacion-completa/reconstruction/cleanroom_t9/contracts/`
in the research checkout, especially `OBSERVE_BOOT.md`, `INSTALLED_MYDLL_BOOT_CANDIDATE.md`
`INSTALLED_MYDLL_BOOT_HOOKS.md`, and `PRE_INIT_WNDPROC_RELAY.md`. This note includes a
new static census, not a new runtime observation.

## MuRet implementation and verification

The [boot-only decision](../../../../docs/adr/0029-mydll-boot-only.md) describes
the intentional differences. To regenerate the checked-in operands on the
development host, pass `gs_oep.asm` and `mydll_cfgload_tbl.h` to
`tools/generate_relocations.py`; it verifies both source digests and the canonical
operand digest before writing the C++ include to standard output.

The VC6 fixture's five-DWORD `f08` negative control left the stack four bytes short
before the pops and restored the wrong `EBX`/`ESI`/`EDI`; the real callback passed
both frame shapes. The synthetic host fixture checked write preflight, the two page
protections, terminal late failures, and an inert repeat. Wrong export ordinal and
wrong foreign-host disposition both failed the loader fixture. The real PE32 DLL
passed the loader fixture and had only `Init @1` and `KERNEL32.dll` imports.
The rejected-host child exited `2` with a diagnostic and no active marker.

An independent raw stack probe failed when the charlist callback was built with
`__cdecl` in both its declaration and definition. The boot-log fixture also caught
the old PID-only marker and verified that a blocked publication path exits `2`
without presenting a new active marker.

These results are from the VC6 toolchain under isolated Wine, not a real
GameServer run. The native bootstrap, observed pre-images at `Init`, stock
map-loader fallback and post-map milestone remain unverified.
