# MyDll: GameServer boot stub

## Goal

Add `Server/MyDll/` as a Windows Server 2003 R2 component. The MU 1.02i repack GameServer
must be able to load the 32-bit `MyDll.dll`, call its only export (`Init`, ordinal 1), and
stay running after bootstrap. A DLL that merely links or exports `Init` does not satisfy
this goal.

The supported host is the GameServer identified in the [boot research](../../../Server/MyDll/docs/research/01-gameserver-boot.md).
Do not add login, packet parsing or forwarding, packet obfuscation, character or gameplay
logic, custom databases, new threads, or protector compatibility. Do not alter the client,
the active installation, or `labs/`.

## Toolchain exception

`Server/MyDll/` is the only exception to MuRet's modern compiler baseline. Use the C++
subset accepted by Visual C++ 6.0 and build the x86 DLL with VC6 on Windows Server 2003 R2.
Keep a standalone VC6 build recipe under the component, with no vcpkg, modern C++ runtime
or dependency on the root x64 presets. The rest of MuRet stays on C++20 and its existing
build. Apply the repository's naming, formatting, resource ownership, evidence and test
rules where they do not require a newer compiler. MyDll must keep this compatibility
throughout its reimplementation, not just through the boot milestone. Modernization is
separate and starts only after the full reimplementation is complete.

## Design

- Do not commit binaries, real game data, INIs, or runtime captures.
- Keep the host-specific boundary inside `Server/MyDll`. `Init` identifies the supported
  host layout and caller, pins the module, and binds the host operands to
  process-lifetime backing. Preflight the expected bytes and slot state before writing.
  The previous boot candidate provides the backing contract, not the implementation to
  copy wholesale. Keep native GS configuration defaults unless a concrete boot
  dependency is shown; do not port the historical gameplay configuration writer.
- Publish only the slots needed for safe boot and closed client traffic. The receive and
  send stubs consume without reading the buffer or forwarding to `ProtocolCore` or
  `DataSend`. `f60` has the required stack contract and leaves every byte unchanged.
  Other mandatory slots do no feature work. Keep the native GS Winsock initialization
  and map-loader continuation rather than importing the historical DLL versions.
- A foreign host is rejected without writes. If initialization fails after identifying the
  supported caller, stop the boot with an explicit failure: the GS does not check the
  return from `Init`. A repeated successful `Init` does not publish twice. Do not support
  hot-swapping or unloading while the GS still holds pointers into the module.

## Verification

1. Write failing contract tests before implementation. Tests that must run on Server 2003
   use a small VC6 fixture instead of building modern GoogleTest into the legacy harness.
   Test foreign-host rejection with no writes, wrong pre-images, repeat initialization,
   the accepted host's slot table, and unchanged buffers. Exercise both observed `f08`
   call shapes; an empty function still has a calling convention.
2. Inspect the VC6-built PE: x86, one export by name and ordinal, and no imports from
   modern Visual C++ runtimes. Record toolchain and artifact hashes. A build made
   elsewhere is not a substitute for the VC6 build check on the Windows 2003 target.
3. Test on a separate copy of the exact GameServer in an isolated environment, against
   the historical boot control under matched conditions. Record identities, PID and
   creation time, successful `Init`, a subsequent GS-owned milestone, and later liveness
   without a blocking dialog. Say where the GS reached. Do not launch a second GS in the
   same environment or change the active one. Obtain approval before changing the VM or
   starting a stack there.

Boot-only success does not require a client connection, a listening game port or a login.
If an environmental failure prevents the real-host check, report the fixture and PE results
separately and leave native boot unverified. Record new findings and intentional differences
from the historical DLL with the component's research and ADRs, not in the tutor/lab tree.
