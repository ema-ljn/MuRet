# 0030. Keep MyDll character selection separate from the boot control

**Status:** Accepted. The selection profile passed native startup without a
client; login and character creation acceptance remain pending.

The original MyDll adapts both character creation and other game traffic. MuRet
keeps `build_vc6.bat dll` as the closed boot control and adds a separate
`selection` artifact for the route from login through character creation.
It delegates authentication and persistence to stock services; there is no
operator switch that opens additional routes in a running server.

Unlike the original, the selection profile denies deletion, map entry and
gameplay, accepts a bounded list shape, and correlates a single pending creation
with the native selector connection and the reply name. On rejection it sends
empty equipment rather than copying bytes which the stock failure path does
not initialize. It enables only the observed `f60` output XOR, leaving C3/C4
to the GameServer and other cipher paths unbound. These choices keep the
profile narrow enough to compare with the boot control and the original.

After the first selection run faulted in Blood Castle, the selection build
restored the repack's `f48` terrain table through the original GameServer's
loader. The closed control retains its seven-slot setup. This change is
terrain initialization only and does not admit world traffic.

See [selection research](../../Server/MyDll/docs/research/04-character-selection.md)
for scope and validation. [ADR 0029](0029-mydll-boot-only.md) still applies to
the closed artifact and the legacy toolchain.
