# MyDll

`MyDll.dll` for the audited MU 1.02i GameServer
(`b2a4521bd7969c61296531551df31e8741cea8235cf5865702f83c51d52a7d5e`).
Both builds export only `Init @1` and install the host's backing.

- **`dll` (baseline):** seven callbacks; the stock map fallback remains unchanged.
  Receive and send stay closed, and the wire callback leaves bytes unchanged.
  Without the observer it does not inspect packets.
- **`selection` (experimental):** delegates login and character creation to the
  GameServer and backends; adapts the list, creation reply and notices for the
  existing client. It restores the repack terrain loader at `f48` and implements
  only the confirmed output XOR. Deletion, entering the map, and gameplay remain
  closed.

## Build with Visual C++ 6.0

From the repository root in a 32-bit Visual C++ 6.0 command prompt
(`VCVARS32.BAT` loaded):

```bat
Server\MyDll\build_vc6.bat dll
Server\MyDll\build_vc6.bat hooks
Server\MyDll\build_vc6.bat host
Server\MyDll\build_vc6.bat load
Server\MyDll\build_vc6.bat log
Server\MyDll\build_vc6.bat observer
build\mydll-vc6\test_boot_hooks.exe
build\mydll-vc6\test_host_boot.exe
build\mydll-vc6\test_library_load.exe build\mydll-vc6\MyDll.dll
build\mydll-vc6\test_boot_log.exe
build\mydll-vc6\test_observer.exe
dumpbin /headers /exports /imports build\mydll-vc6\MyDll.dll
```

To build and test the selection DLL without overwriting the boot-only artifact:

```bat
Server\MyDll\build_vc6.bat selection
Server\MyDll\build_vc6.bat selection-tests
build\mydll-vc6\test_selection.exe
build\mydll-vc6\test_selection_gateway.exe
build\mydll-vc6\test_terrain_boot.exe
build\mydll-vc6\test_library_load.exe build\mydll-vc6\selection\MyDll.dll
dumpbin /headers /exports /imports build\mydll-vc6\selection\MyDll.dll
```

The x86 build is independent of MuRet's CMake and vcpkg configuration. The
fixtures do not load or launch the GameServer. For the terminal failure fixture,
`test_host_boot.exe --reject-child` should exit `2` with a fresh
`mydll-boot-<pid>-<creation-filetime>.log` containing `MYDLL_BOOT_REJECT` and
`stage=operand_preimage`.

## Boot evidence

Use only a separate copy of the audited host in an isolated environment. Never replace
the active DLL or hot-swap this one. An unrelated loader receives `FALSE` without
a marker; the recognized GS exits nonzero on a rejected boot.

`MYDLL_BOOT_ACTIVE` in `mydll-boot-<pid>-<creation-filetime>.log` shows that `Init` published
the backing and callbacks. The working directory needs to be writable for a complete
marker to be published. It does not show that the GameServer reached the map or
shop load, opened a port, or accepts logins. A native boot-only probe published
the marker and wrote observer rows but exited before the first process sample.
The boot-only build has not passed sustained native boot acceptance.

The selection build also writes `mydll-maps-<pid>-<creation-filetime>.log`.
`MYDLL_MAPS_LOADED COUNT=29` confirms the terrain callback completed. In the
[native selection trial](docs/research/04-character-selection.md#native-boot-result-selection-9dd9dd10),
the same process stayed alive and listened on `55901` at 35 seconds. This
establishes startup for that selection artifact, not login or character creation.

See the [boot research](docs/research/01-gameserver-boot.md) and the
[implementation plan](../../docs/superpowers/plans/2026-11-26-mydll-boot-stub.md).

## Observe traffic

Copy [`debug_mydll.ini.example`](debug_mydll.ini.example) to `debug_mydll.ini`
beside `GameServer.exe` in the isolated copy. Set `Enabled=1` before starting the
GameServer. The DLL reads it at boot; edits are not reloaded while it runs.
If the INI is absent or disabled, no observer is installed.

| `[Observer]` option | Default | Effect |
| --- | ---: | --- |
| `Enabled` | `0` | Enable observation with `1`. |
| `Receive` | `1` | Record client-to-GS messages at the receive callback. |
| `Send` | `0` | Record GS-to-client messages at the send callback. |
| `IncludeSubcode` | `1` | Include the subcode only for eligible `C1 F1` and `C1 F3` headers. |
| `LogMalformed` | `1` | Include short, malformed and unreadable headers as diagnostic rows. |
| `MaxRows` | `4096` | Cap records per run, at most `65536`; zero uses the default. |

The log is `mydll-observe-<pid>-<creation-filetime>.tsv` beside the executable.
Rows contain direction, decision, frame, opcode, eligible subcode, length
and header status. They contain no credentials, names, player indices or payload.
The boot-only build writes `BLOCKED_BY_STUB`. The selection build writes
`BLOCKED_BY_PROFILE` or `DELEGATED_TO_GS`. Delegated TX rows show the adapted
length before wire obfuscation; delegation does not prove delivery or login.
A live reader must allow shared writing. The observer stops at the row limit;
`busy_drops` counts calls missed under contention since the previous recorded row.
See the [observation contract](docs/research/02-observation.md) for coverage limits.

Observation does not change what either build admits. It cannot report traffic
rejected by the GS before the hook. See [selection research](docs/research/04-character-selection.md)
for the admitted routes and native test gates.
