# MyDll native reimplementation

This is a fidelity-first restart for the audited MU 1.02i GameServer. The sole
public export is `Init @1`. It does not yet install callbacks or claim native
boot, login, character selection, or gameplay acceptance.

The subject is `MyDll_original.dll`, SHA-256
`910966f738797b0508483c12c9619346c8760243df24d6db3d36780b43870fa1`.
The installed reference is a separate artifact, SHA-256
`8b39364e6720632b308e48246043668d6a0db0d74f05260763ce8d40919d16e9`.

## Current scope

Three native byte-order functions are linked and proved exact against both
references: 33, 31, and 31 instructions, totaling 275 bytes without relocations.
The linked bodies match the original too. Ninety-seven behavioral cases pass.
`cmd_is_slash` and `f60` have exact object bodies but are not linked yet.

The port ledger is [native-functions.tsv](research/native-functions.tsv).
`exact` means the measured instructions match, not merely equivalent behavior.
`not_linked` or `pending` never means accepted. This is not whole-DLL identity:
imports, CRT/MFC, sections, timestamps, and pending functions differ by design.
License, protection, and virtual-machine mechanisms remain out of scope.

## Build and verify

From the repository root in a Visual C++ 6.0 command prompt:

```bat
Server\MyDll\build_vc6.bat dll
Server\MyDll\build_vc6.bat test
build\mydll-vc6\test_byte_order.exe
```

The standalone build uses VC6 `/W3 /Od /GX /MT` for the native bodies. Its
standalone exception from the repository's modern C++ rules is intentional.
Keep original identifiers and loop structure where they affect the emitted
x86 bytes.

To compare the linked functions and ledger, pass the pinned original,
installed reference, DLL, map, and ledger to `tools/verify_native_fidelity.py`.
Do not add original binaries, generated objects, map files, or runtime captures
to Git.
