# Stub observation

## Boundary

`f28` (`0x00414170`) and `f34` (`0x00440230`) are the audited GS's receive and
send selectors. In the boot-only profile the optional wrappers observe their
arguments and delegate only to the existing closed stubs. They preserve the buffer, return
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

### Isolated Windows Server 2003 R2 probe

The `3d83e064` build (`699bb797…d604`, built with VC6 under Wine and
verified by read-back) ran once in `C:\mx_debug_mydll_490\GameServer`.
The clone had no NIC, MU backends, or client. `Enabled`, `Receive`, and `Send`
were all `1`. The audited GS was `b2a4521b…7d5e`.

- `MYDLL_BOOT_ACTIVE PID=2484 START=01dd7d69788ba07c RELOC=2814 SLOTS=7`.
  Boot marker SHA-256: `aceb9d221cf90374000ed5b57330fa3eb35fd1cc6d2d22828722b7250f2b9aa8`.
- The TSV was opened and contained 15 `TX` attempts with the same shape
  (`C1`, `9B`, 14 bytes), all `BLOCKED_BY_STUB`, and no `RX` rows.
  TSV SHA-256: `a6ce32c988940bcd927c5e2918faec5eac65a7dd01a09331390cd4ac71de4c7d`.
- The GS was already absent at the first process sample and was absent at the
  second. No exit cause or stock startup milestone was established. This
verifies native `Init` and observer output, not a sustained boot or any
client action. The original laboratory server was not modified.

See the [original-stack character creation baseline](03-character-create-baseline.md)
for the later controlled VMXP action. That run did not load this observer.
The [selection trial](04-character-selection.md#native-boot-result-selection-9dd9dd10)
records the distinct `BLOCKED_BY_PROFILE` outcomes from its native boot.
