# MyDll

Boot-only `MyDll.dll` for the audited MU 1.02i GameServer
(`b2a4521bd7969c61296531551df31e8741cea8235cf5865702f83c51d52a7d5e`).
It exports only `Init @1`, installs the host's backing and seven inert callbacks,
and leaves the native map-loader slot unset. It does not implement login, packet
parsing, forwarding, obfuscation, gameplay, or a database.

## Build with Visual C++ 6.0

From the repository root in a 32-bit Visual C++ 6.0 command prompt
(`VCVARS32.BAT` loaded):

```bat
Server\MyDll\build_vc6.bat dll
Server\MyDll\build_vc6.bat hooks
Server\MyDll\build_vc6.bat host
Server\MyDll\build_vc6.bat load
build\mydll-vc6\test_boot_hooks.exe
build\mydll-vc6\test_host_boot.exe
build\mydll-vc6\test_library_load.exe build\mydll-vc6\MyDll.dll
dumpbin /headers /exports /imports build\mydll-vc6\MyDll.dll
```

The x86 build is independent of MuRet's CMake and vcpkg configuration. The
fixtures do not load or launch the GameServer. For the terminal failure fixture,
`test_host_boot.exe --reject-child` should exit `2` with a fresh
`mydll-boot-<pid>.log` containing `MYDLL_BOOT_REJECT` and `stage=operand_preimage`.

## Boot evidence

Use only a separate copy of the audited host in an isolated environment. Never replace
the active DLL or hot-swap this one. An unrelated loader receives `FALSE` without
a marker; the recognized GS exits nonzero on a rejected boot.

`MYDLL_BOOT_ACTIVE` in `mydll-boot-<pid>.log` shows that `Init` published
the backing and callbacks. It does not show that the GameServer reached the map or
shop load, opened a port, or accepts logins. This change was built with VC6 and tested
with synthetic fixtures under isolated Wine. Native Windows Server 2003 R2 build
and GameServer boot acceptance remain open.

See the [boot research](docs/research/01-gameserver-boot.md) and the
[implementation plan](../../docs/superpowers/plans/2026-11-26-mydll-boot-stub.md).
