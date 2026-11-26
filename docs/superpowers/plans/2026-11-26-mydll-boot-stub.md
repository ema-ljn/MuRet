# MyDll Boot Stub Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans for native execution, or superpowers:subagent-driven-development if Emanuel selects it. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a 32-bit `MyDll.dll` that the exact MU 1.02i GameServer can load and boot with, without client or gameplay features.

**Architecture:** Keep the inert callbacks separate from host preparation. The one public entry point verifies the host, installs its backing, and publishes callbacks last.

**Tech Stack:** Visual C++ 6.0, x86 Win32, Windows Server 2003 R2, standalone batch build, native VC6 test executables.

**Spec:** [MyDll: GameServer boot stub](../specs/2026-11-26-mydll-boot-stub-design.md).
**Research:** [GameServer boot contract](../../../Server/MyDll/docs/research/01-gameserver-boot.md).

## Global Constraints

- Keep VC6 compatibility throughout the MyDll reimplementation. Leave the root C++20 build, the client, and `labs/` unchanged.
- Export only `Init` at ordinal 1. No login, default packet inspection, forwarding, obfuscation, character handling, gameplay logic, custom database, new threads, or protector compatibility. The optional INI-enabled observer only records bounded metadata and delegates to the closed callbacks.
- No changes to GS configuration defaults. No binaries, real game data, INIs, or runtime captures in Git. New code and docs use the repository's English naming and style.
- No guest copy, execution, networking change, or stack startup without the separately approved VMware commands. Do not change the active server or run two GameServers in one environment.

## Review Focus

| Risk | Acceptance check |
| --- | --- |
| `f08` can corrupt saved registers even when final `ESP` looks right | Task 1 checks the adjustment before the pops, `EBX`/`ESI`/`EDI`, and the outer return at both continuations. |
| An inert network callback still reads an argument buffer | Task 1 calls every buffer-taking stub with a no-access pointer; it also checks real sentinel bytes. |
| A foreign caller gets terminated, or a recognized GS continues after a late failure | Tasks 2–3 cover both dispositions, including an injected failure after the first write. |
| A four-byte operand straddles pages with different permissions | Task 2 checks both protections after success and does not admit a host with a failed restore. |
| A fixture pass or a process blocked in a dialog is reported as a real boot | Task 3 requires the same PID, a later GS-owned milestone, and a matched `CLOSED` control. |

### Task 1: Inert callbacks and x86 ABI

**Files:** `Server/MyDll/build_vc6.bat`, `src/BootHooks.h`, `src/BootHooks.cpp`, `tests/test_boot_hooks.cpp` (all source and test paths below are under `Server/MyDll/` unless named from the root).

**Interfaces:** Internal `MyDll::Boot` callbacks. A local `.def` will export only the public entry point in Task 3, never these symbols.

| Slot | Signature or machine contract | Expected behavior |
| --- | --- | --- |
| `f08` | `AreaStub`: private x86 entry with no C++ argument access | `EAX = 0`. At `0x00415b7e` pop eight argument bytes; at `0x00427975` pop four, then the caller adds the remaining sixteen before restoring its registers. |
| `f0c` | `void __stdcall DiscardCharacterList(DWORD)` | No read, write, or call-out. |
| `f24` | `int __stdcall RejectSetCharacter(DWORD, DWORD)` | Return `0`. |
| `f28` | `DWORD __cdecl DiscardReceive(DWORD, DWORD, DWORD, int, DWORD, DWORD)` | Return `0x80004005`; do not call the native dispatcher. |
| `f2c` | `int __stdcall NoDuel(DWORD)` | Return `0`. |
| `f34` | `DWORD __cdecl DiscardSend(int, char*, DWORD)` | Return `1` as a consumed send; do not call the native sender. |
| `f60` | `void __stdcall LeaveBytesUnchanged(unsigned char*, int)` | Do not read or write bytes, even for a nonzero length. |

- [ ] **RED:** Create `test_boot_hooks.cpp` and first link a deliberately wrong `f08` that pops zero bytes on the five-DWORD path. Use fixture-owned, address-verified landings for both sites. Model `0x00427977–80`, including the pops before `ESP` is reset. Assert `ESP` before the pops, exact `EBX`/`ESI`/`EDI` sentinels after them, neutral `EAX`, and a correct outer return. Verify the two-DWORD path resumes at `0x00415b83` with both arguments removed. Check the other call conventions and unchanged sentinel bytes; use a no-access pointer for buffer-shaped arguments. The `f28`/`f34` results are closed-stub choices inherited from the old candidate, not runtime observations of GS consumption.
- [ ] **GREEN:** Implement the inert callbacks. Keep the assembly in `BootHooks.cpp`; the thunk distinguishes the two known callers by return address and does not inspect any arguments. Use named constants. Keep fixture checks active in release builds, independently of `NDEBUG`.
- [ ] **Verify:** From the repo root with `VCVARS32.BAT` loaded, run `Server\MyDll\build_vc6.bat hooks` and `build\mydll-vc6\test_boot_hooks.exe`. Require the red run to show a failed assertion and the green run to report `PASS boot hooks` with exit `0`. The standalone driver uses VC6 `/MT /W4 /WX`, checks x86, and writes only to the ignored build directory.

### Task 2: Exact-host preflight, backing, and slot publication

**Files:** `src/HostBoot.h`, `src/HostBoot.cpp`, `src/GameServer102i.cpp`, `src/GameServer102iRelocations.inc`, `tests/test_host_boot.cpp`, `build_vc6.bat`.

**Interfaces:** `MyDll::Boot::InstallBoot(const HostLayout&, const BootInvocation&, BootSession&, bool (__cdecl* prepareResources)(), BootFailure&) -> BootResult`. `BootInvocation` holds the main image base, image size, actual return address, and resolved `Init` address. `HostLayout` holds required signatures, named slot addresses, and `Relocation` rows (`DWORD operandAddress`, `DWORD expectedValue`, `DWORD backingOffset`). `BootSession` starts cold and a write failure leaves it terminal. `BootResult` distinguishes `ForeignHost`, `Rejected`, `Installed`, and `AlreadyInstalled`. `BootFailure` gives a stage and, if available, the first failing row, address, expected and observed DWORD. `GameServer102iLayout()` returns the production layout; only the fixture supplies a synthetic one.

- [ ] **RED:** Build a synthetic current-process image with a caller encoding that names the fixture's real `Init` cell, zero slots and two relocation rows. Assert `Installed`, exact `backing + offset` operands, seven expected slot pointers, unchanged surrounding canaries and `f48 == 0`; repeat requires `AlreadyInstalled` and one resource call total. Wrong image/base, caller address, signature or `Init` cell must produce `ForeignHost` without resource calls or writes. Bad late pre-image, bad continuation, overflow, no-access/guard/uncommitted page, occupied publication slot or `f48`, and resource failure must produce `Rejected` with no writes. Include a crossing operand with differently protected pages, then inject second-page protection, restore, cache-flush and late slot-conflict failures via a fixture-only seam and assert terminal rejection, no active state and no successful retry.
- [ ] **GREEN:** Identify the main PE32 image extent `[0x00400000, 0x081dc000)`, actual return `0x0048f736`, six bytes at `0x0048f730`, and resolved export in `0x073112ac` before pinning or authorizing termination. This is a layout guard, not the external EXE digest. Preflight the RX/TX selectors and native entries, the two `f08` continuations, and the `f48` fallback. Check allocation ownership, bounds and page protections before reading any span. Require seven aligned, writable, zero publication cells and separately a readable zero `f48`. The fixture may simulate the layout; the public entry takes only the actual main image.
- [ ] **GREEN:** Use one process-lifetime backing: items at `0x00000000`, Blood Castle at `0x0376a100`, objects at `0x0376b400`, and monster definitions at `0x04941a80`, total `0x04955a8c`. Derive 69/2,550/159/36 relocation rows by absolute byte address from the four bank tables and OEP ASM. Preserve bank and signed delta until validation, normalize just the four monster `-1` rows, exclude CONFIG and the `Extra` banks, and reject overlapping writes. Validate write sites and derived offsets, not old DWORDs as targets. Record the derivation and match the canonical triples hash in the research; VC6 consumes only the derived table.
- [ ] **GREEN:** Validate everything and prepare resources before the first host write. Restore both protections of crossing writes and flush the instruction cache after code changes. Read back operands and publish/read back slots last, with no `f48` or gameplay config write. A fault after mutation is terminal, not an invitation to retry or to run with mixed state; `Init` will terminate the recognized GS. Never dereference an unreadable page just to populate a diagnostic.
- [ ] **Verify:** Run `Server\MyDll\build_vc6.bat host` and `build\mydll-vc6\test_host_boot.exe`. Require a meaningful red result before implementation, then `PASS host boot` with exit `0`. Check the real table counts and confirm all six observed cross-page object operands are represented. Win32 protection behavior was checked with Context7: [VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect) reports only the first page's previous protection; [FlushInstructionCache](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushinstructioncache) applies after executable memory changes.

### Task 3: Public DLL, native boot evidence, and handoff

**Files:** `src/Init.cpp`, `src/MyDll.def`, `tests/test_library_load.cpp`, `build_vc6.bat`, `Server/MyDll/README.md`, `Server/MyDll/docs/research/01-gameserver-boot.md`, `docs/adr/0029-mydll-boot-only.md`, `docs/adr/README.md`.

**Interfaces:** `extern "C" BOOL __cdecl Init(void)`, name `Init`, ordinal 1. The VC6 entry shim captures the actual caller return address before a nested call or prologue can change its meaning. `DllMain` may record the module handle but does no preparation. A successful run emits fresh `MYDLL_BOOT_ACTIVE` with a PID and counts; an identified failure emits `MYDLL_BOOT_REJECT`, first failed check and known row details, then exits nonzero. Neither marker contains packet data, identifiers of player slots, or credentials.

- [ ] **RED:** Write `test_library_load.cpp`. Start with wrong-ordinal and wrong-disposition doubles to confirm the assertions fail. The DLL test resolves name and ordinal 1 to the same address, checks that an ordinary foreign process returns `FALSE` without a marker, releases the DLL and verifies normal exit. A disposable recognized-host test of the same entry disposition rejects a bad row, records the stage, exits nonzero and emits no active marker. Do not export an alternate production entry for tests.
- [ ] **GREEN:** Pass the actual caller and main image details to `InstallBoot`. Pin the DLL for the life of the identified GS, but do not create sockets or call `WSAStartup` in the initial stub; native Winsock initialization belongs to the GS. An identified failure logs only scoped diagnostics and exits nonzero; a foreign host returns `FALSE` without logging or host writes. A repeated success is inert.
- [ ] **Verify the artifact:** On Server 2003 run `Server\MyDll\build_vc6.bat dll`, `Server\MyDll\build_vc6.bat load`, and `build\mydll-vc6\test_library_load.exe build\mydll-vc6\MyDll.dll`. Run `dumpbin /headers /exports /imports build\mydll-vc6\MyDll.dll`. Require `PASS DLL loader`, compiler `12.00`, recorded linker banner, machine `14C`, PE32 DLL characteristics, sole non-forwarded export `Init @1`, and only authorized `KERNEL32.dll` imports. Check imported symbols against the target runtime and reject networking and thread-creation imports. Record the staged DLL hash after reading it back.
- [ ] **Verify native boot, after approval:** Use a separate copy in the no-NIC VM with no GS and no MU backends running, under separately approved commands. Match the GS hash, data/INI, command line, and environment for the old `CLOSED` control and new stub. Require a fresh active marker, same PID/creation time alive at least 30 seconds later, no blocking dialog, and a stock milestone at or beyond `GameMainInit`'s map-load join (`0x0048a6b6`, followed by `ShopDataLoad` at `0x0048a6c0`) evidenced by an existing log or read-only state. Record the farthest independently observed step in each run. A listener, backend connection, or client login is not an acceptance criterion. If neither run reaches the milestone, report the environmental blocker; do not claim native boot on liveness alone.
- [ ] **Isolate a divergent native result:** If needed, reproduce the fixed `CLOSED` control from frozen inputs before building a one-variable subtraction ladder outside MuRet: inert `f60`, null `f48`, omit the 65 CONFIG writes, omit the DLL's Winsock startup, then replace the remaining non-inert callbacks one at a time. Compare the new installer only after those cuts are isolated. Run each authorized rung with the same GS, data and VM/backend state. Compare the common milestone and normalized operands, slot occupancy and shared map state, not pointer equality or all map bytes. Keep obsolete behavior and diagnostic variants out of the MuRet tree. Leave an identified dependency as an evidence-backed design decision rather than silently restoring an excluded feature.
- [ ] **Publish usage and evidence:** Document the VC6 build and how to distinguish successful `Init` from a running GS. Record the intentional differences (closed stubs, no XOR, native map-loader fallback, no historical configuration writer) and the actual verification scope; link the ADR in its index and from the research. Run `git diff --check`. The user authorized a PR on a separate branch, not merging, deployment, or changes to the untracked CL7 plan.

## Draft PR checkpoint

- Task 1: VC6/Wine red and green observed; the zero-pop `f08` double reproduced the incorrect saved registers.
- Task 2: VC6/Wine red and green observed; the synthetic fixture passed preflight, relocation, publication,
  and terminal fault cases. The deterministic include matched the canonical research census.
- Task 3: VC6/Wine export and loader checks passed, including the two negative loader doubles and the rejected
  child process. The native VC6 build and matched control/stub acceptance on Windows Server 2003 R2 remain open.
- Follow-up: [Optional stub observation](../../../Server/MyDll/docs/research/02-observation.md) uses the same DLL
  and does not change the native acceptance gate.
