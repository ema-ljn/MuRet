# Original MyDll 1.02i function census

This census is context for the [boot contract](01-gameserver-boot.md). It
replaces an earlier tally that counted procedure entries, internal labels, and
virtual-machine regions as if they were all functions. The
[port roadmap](coverage/port-roadmap.tsv) tracks the work separately.

## Artifact and method

The subject is the original `MyDll_original.dll`, PE32 x86 at preferred base
`0x10000000`, SHA-256
`910966f738797b0508483c12c9619346c8760243df24d6db3d36780b43870fa1`.
The installed DLL from the boot research (`8b39364e…16e9`) is a separate artifact:
it differs by one byte. All 258 cataloged trampoline windows and the 256-entry
dispatch table match in both files. This comparison does not replace the installed
DLL or the GameServer call sites as sources for the host ABI.

A fresh REA/Ghidra 12.1.4 analysis recovered the same 1,737 internal entry
addresses as the archived export. REA also reported 372 external procedures;
those are not DLL implementations or the PE's 373 import slots. The rerun checked
the archived instruction bytes against the original PE and reconciled both entry
sets with the research inventory. Claude Opus 5.5 (`xhigh`) independently reviewed
the counting method and its limits; it did not verify binary bytes.

## What the counts mean

| Count | Unit and interpretation |
| ---: | --- |
| 2,477 | Address rows in `coverage.tsv`, including 740 addresses not recovered as Ghidra entries. |
| 2,489 | Ledger subjects: the 2,477 address rows, nine VM region records, two table records, and one external dependency record. |
| 398 | Procedure entries cataloged as application-owned. This is a provisional inventory, not a total of all logical functions. |
| 362 | Application entries with no VM boundary observed in the recovered native body. This is a bounded static finding, not proof about callees or indirect paths. |
| 3 | Mixed bodies: `Init` (`0x10001d14`), receive dispatch (`0x1000c6a7`), and CityWar (`0x10019898`). |
| 5 | Native setup wrappers that transfer to VM. The observed preambles do not establish the extent of the VM bodies. |
| 28 | Application entries whose body classification remains undetermined. |

`0x10002212` is a continuation of `Init`, not an additional function. `0x10002f28`
is a tail fragment even though Ghidra lists it as an entry. The separate GS callback
at `0x1002b5cb` has a byte-checked body and a native reference but is absent from
both Ghidra entry sets.

The 258 trampolines and 47 distinct dispatch-table destinations are not function
counts. The research catalog attributes 164 trampolines by static edges; 94 lack
that attribution. Inside receive dispatch, the region at `0x1000cad3` has 81
edge-attributed entries and 44 additional layout candidates. Those 44 do not prove
ownership, protocol behavior, or execution. There is no defensible total for fully
virtualized logical functions.

The census keeps the 1,468 static-library address rows separate from the 165
rows attributed to compiler support or template instantiation. Those attributions
are catalog labels, not a fresh toolchain provenance audit. The independent PE
check resolved 375 import jumps against the IAT; the 13 relative jumps include two
internal tails outside `coverage.tsv`. Jump shape alone does not identify the
compiler or linker that emitted it.

## Coverage warehouse

| Table | Unit and join key |
| --- | --- |
| [Original addresses](coverage/original-addresses.tsv) | One observed or cataloged address per `va`, including non-function sites. |
| [Original functions](coverage/original-functions.tsv) | One candidate per `function_id` (`mydll!` plus RVA); `va` joins the address table. Filter `cataloged_origin` to `cataloged_application` for the 398 entries above. |
| [VM regions](coverage/original-vm-regions.tsv) and [trampolines](coverage/original-vm-trampolines.tsv) | One row per region or physical stub. `parent_va` and `attributed_parent_va` join to the original function; a layout candidate is not a confirmed join. |
| [Toolchain](coverage/original-toolchain.tsv) | Cataloged library/compiler roles and observed jump sites, keyed by `va`. |
| [Other subjects](coverage/original-other-subjects.tsv) | Table or external dependency; never add these to a procedure count. |
| [Relations](coverage/original-relations.tsv) | Reviewed input for fragments, additional entries, and region ownership. The generator verifies the relations it can against the original PE. |
| [Port roadmap](coverage/port-roadmap.tsv) | Hand-maintained work items. Join `original_function_id` to the function table for context, not as a mandate to port that function. Update `state` and `next_gate` only with MuRet evidence. |

Port work still needs evidence for each host callback, wire contract, and runtime
milestone. An address, a catalog label, or a static edge does not establish
reachability or parity. License and protection code remain outside the port.

### Regenerate

Use Python with `pefile==2024.8.26` in an isolated environment. Inputs come from
the [repack research checkout](https://github.com/EmaNLJN/mu-server-repack-research-102i)
and its local artifacts. `pefile` reads the PE without launching it and is only
needed by this offline tool, not the DLL build or runtime. To collect a fresh REA bundle, open the original with
the `ghidra` provider, list procedures, export evidence, and close the session.
The original evidence ID and input/output hashes are in the
[manifest](coverage/original-manifest.json). Nothing is sent to REA at
generation time; the generator reads an exported bundle.

From the MuRet root, with `REPACK` set to the external checkout and
`REA_EVIDENCE` set to the bundle:

```sh
python3 -B -m unittest discover -s Server/MyDll/tools/tests
out=$(mktemp -d)
python3 -B Server/MyDll/tools/generate_original_coverage.py \
  --research-root "$REPACK" --rea-evidence "$REA_EVIDENCE" --out "$out/generated"
(
  for file in "$out/generated/"*; do
    diff -u "Server/MyDll/docs/research/coverage/${file##*/}" "$file" || exit 1
  done
)
```

Review generated diffs before replacing the tracked snapshots. A newly exported
bundle can change its evidence ID and hash even if the procedure address set is
unchanged. Do not regenerate `original-relations.tsv` or `port-roadmap.tsv`
over manual edits. Raw binaries, evidence bundles, and model transcripts are
not tracked in MuRet.
