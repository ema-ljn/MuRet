# 0029: Keep the first MyDll build boot-only

**Status:** Accepted scope (Emanuel, 2026-11-26); native boot evidence pending.

## Context

The 1.02i GameServer ignores the return value of `Init @1` and does not check all
hook pointers before calling them. A historical `CLOSED` candidate published eight
callbacks, changed 65 configuration values, and kept the packet transform. That
run does not show that each action is required. See the
[boot contract](../../Server/MyDll/docs/research/01-gameserver-boot.md).

## Decision

Use VC6 and Windows Server 2003 R2 throughout MyDll's reimplementation. The first
component installs only host backing and inert callbacks. `f60` leaves bytes
unchanged, `f48` stays unset for the native map loader, and MuRet leaves the GS
configuration and socket setup to the host. The `f08` adapter preserves the
caller's saved registers in both evidenced x86 frames.

## Consequences

The DLL is not a replacement for an active server. The native GS must be tested
separately; loading under Wine and a boot marker do not prove that the stock startup
progressed. Fixtures check the x86 ABI, guarded publication, foreign-host rejection,
and the artifact's named and ordinal export.
