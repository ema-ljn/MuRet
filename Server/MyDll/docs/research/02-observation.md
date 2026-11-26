# Stub observation

## Boundary

`f28` (`0x00414170`) and `f34` (`0x00440230`) are the audited GS's receive and
send selectors. The optional wrappers observe their arguments and delegate
only to the existing closed stubs. The wrappers preserve the buffer, return
value and `LastError`; `f60` and the other hooks are unchanged. They neither
dispatch to stock handlers nor examine whether a stock handler would succeed.

The archived sources are `gameserver/disasm/gs_oep.asm`,
`gameserver/docs/08_recepcion_contratos.md`, and `gameserver/docs/12_despacho_hook_envio.md`
in the pinned [research checkout](01-gameserver-boot.md). In particular, the
reported length is the argument from the GS, not a TCP frame size. On RX, C3
processing can already have built a C1-shaped header whose declared size differs
from the argument. The observer never extends the readable span using the
declared length.

## Rows and limits

- Read only the header needed for C1/C2. A fourth C1 byte is considered a subcode
  only for F1/F3, when present by both supplied and declared lengths. C3/C4
  handed to the wrapper as C3/C4 are `opaque`; no opcode is guessed from ciphertext.
- Unavailable fields are `-`. `ok` describes the header inspected, not successful
  parsing, handling, sending, or authentication. `BLOCKED_BY_STUB` is not a list
  of missing native handlers. No payload, credential, name, address, player index,
  encryption parameter, or sequence supplied by the peer is emitted.
- The observer is off if the INI is missing, disabled, or neither direction is
  enabled. If the TSV cannot be opened, closed callbacks stay in place and a debug
  message is emitted. A write failure stops recording without changing disposition.
  A TSV header alone does not prove that `Init` or the GS completed boot.
- The per-run row cap bounds output. A busy writer skips contending callbacks and
  reports the count on the next written row, if there is one. No absence in this
  log proves absence on the wire. Configuration and the TSV live beside the GS
  executable, independently of the working directory.

For a future permitted path, record the routing decision separately. Start from
an operator action that the client actually reached, match its observed family to
the target ASM, then document and test the handler before opening that path.
A closed login can prevent an attempt to create a character (including Dark Lord)
from ever reaching this boundary.

## Verification

The VC6 fixture under isolated Wine checks the default-off INI, independent
RX/TX options, subcode restriction, opaque and unreadable input, row limits,
payload privacy, return values and `LastError`. The synthetic host fixture
checks that only selected slots are replaced. A separate VC6 test process
checks that the INI and log follow its executable, not its working directory.
This is not a real client trace or a native Windows Server 2003 R2 boot.
