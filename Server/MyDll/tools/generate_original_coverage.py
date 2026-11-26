"""Generate the original MyDll coverage tables without loading either DLL."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

import pefile

COVERAGE_DIR = Path(__file__).resolve().parents[1] / "docs/research/coverage"
ORIGINAL = "910966f738797b0508483c12c9619346c8760243df24d6db3d36780b43870fa1"
INSTALLED = "8b39364e6720632b308e48246043668d6a0db0d74f05260763ce8d40919d16e9"
DISPATCHER = 0x1000216C
DISPATCH_TABLE = 0x1000CADD
ASM = re.compile(r"^([0-9a-f]{8})\s+([0-9a-f]+)\s+(\S+)(?:\s|$)", re.MULTILINE)
OBJDUMP_BYTES = re.compile(r"^([0-9a-f]{8}):\t((?:[0-9a-f]{2}(?: |$))+)", re.MULTILINE)
BUCKETS = {"app", "mfc_crt", "thunk", "generado", "vm", "proteccion", "crack"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def va(text: str) -> int:
    return int(text, 16)


def fmt(address: int | None) -> str:
    return "" if address is None else f"0x{address:08x}"


def table(path: Path, delimiter: str = "\t") -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as file:
        return list(csv.DictReader((s for s in file if s.strip() and not s.startswith("#")), delimiter=delimiter))


def index(rows: list[dict], key: str, number: bool = False) -> dict:
    def identify(row: dict) -> str | int:
        return va(row[key]) if number else row[key]

    keyed = {identify(row): row for row in rows}
    require(len(keyed) == len(rows), f"Duplicate key: {key}")
    return keyed


def emit(path: Path, rows: list[dict]) -> None:
    require(bool(rows), f"Empty table: {path.name}")
    with path.open("x", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, rows[0], delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


@dataclass(frozen=True)
class Instruction:
    address: int
    code: bytes
    op: str

    @property
    def end(self) -> int:
        return self.address + len(self.code)

    def direct(self) -> int | None:
        code = self.code
        if (
            (len(code) == 5 and code[0] in (0xE8, 0xE9))
            or (len(code) == 6 and code[0] == 0x0F and 0x80 <= code[1] <= 0x8F)
        ):
            return self.end + int.from_bytes(code[-4:], "little", signed=True)
        if len(code) == 2 and (0x70 <= code[0] <= 0x7F or code[0] == 0xEB or 0xE0 <= code[0] <= 0xE3):
            return self.end + int.from_bytes(code[-1:], "little", signed=True)
        return None


class Image:
    def __init__(self, path: Path, expected: str):
        self.digest = sha(path)
        require(self.digest == expected, f"Unexpected artifact SHA-256: {path.name}")
        self.pe = pefile.PE(str(path))
        require(self.pe.FILE_HEADER.Machine == 0x14C and self.pe.OPTIONAL_HEADER.Magic == 0x10B, "Expected PE32 x86")
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.imports = {
            item.address: group.dll.decode("ascii") + "!" + (item.name.decode("ascii") if item.name else str(item.ordinal))
            for group in self.pe.DIRECTORY_ENTRY_IMPORT
            for item in group.imports
        }

    def read(self, address: int, size: int) -> bytes:
        return self.pe.get_data(address - self.base, size)

    def section(self, address: int) -> str:
        section = self.pe.get_section_by_rva(address - self.base)
        return section.Name.split(b"\0", 1)[0].decode("ascii", "replace") if section else ""

    def executable(self, address: int) -> bool:
        section = self.pe.get_section_by_rva(address - self.base)
        return section is not None and bool(section.Characteristics & 0x20000000)

    def stubs(self) -> dict[int, str]:
        result = {}
        for section in self.pe.sections:
            data = section.get_data()
            base = self.base + section.VirtualAddress
            for offset in range(len(data) - 9):
                if data[offset] == 0x68 and data[offset + 5] == 0xE9:
                    target = base + offset + 10 + struct.unpack_from("<i", data, offset + 6)[0]
                    if target == DISPATCHER:
                        result[base + offset] = self.section(base + offset)
        return result

    def handler_slots(self) -> Counter:
        return Counter(struct.unpack("<256I", self.read(DISPATCH_TABLE, 1024)))


def read_asm(static_dir: Path, functions: dict, original: Image) -> tuple[dict, dict, dict]:
    bodies = {}
    owners: defaultdict[int, set[int]] = defaultdict(set)
    input_hashes = {}
    for entry, row in functions.items():
        path = static_dir / row["asm_file"]
        raw = path.read_bytes()
        input_hashes[row["asm_file"]] = hashlib.sha256(raw).hexdigest()
        text = raw.decode("utf-8")
        require(original.digest in text.splitlines()[1], f"ASM not tied to original: {fmt(entry)}")
        code = [Instruction(va(address), bytes.fromhex(chunk), op) for address, chunk, op in ASM.findall(text)]
        require(len(code) == row["instructions"] and any(i.address == entry for i in code),
                f"Incomplete export: {fmt(entry)}")
        require(
            all(original.read(i.address, len(i.code)) == i.code for i in code),
            f"ASM bytes do not match original: {fmt(entry)}",
        )
        bodies[entry] = code
        for instruction in code:
            owners[instruction.address].add(entry)
    return bodies, owners, input_hashes


def verify_objdump(path: Path, entry: int, size: int, original: Image) -> None:
    chunks = [(va(address), bytes.fromhex(chunk)) for address, chunk in
              OBJDUMP_BYTES.findall(path.read_text(encoding="utf-8"))]
    require(bool(chunks) and chunks[0][0] == entry, f"Objdump missing entry: {fmt(entry)}")
    for address, code in chunks:
        require(address == entry and original.read(address, len(code)) == code,
                f"Objdump bytes do not match original: {fmt(address)}")
        entry += len(code)
        size -= len(code)
    require(size == 0, f"Unexpected objdump extent: {path.name}")


def read_spans(path: Path) -> tuple[dict, dict]:
    spans: defaultdict[int, list[tuple[int, int, str]]] = defaultdict(list)
    fragments = {}
    for row in table(path):
        owner, lo, hi, kind = va(row["va_funcion"]), va(row["lo"]), va(row["hi"]), row["tipo"]
        require(lo < hi and kind in {"nativo", "vm", "marcador", "stubs"}, f"Invalid span: {row}")
        spans[owner].append((lo, hi, kind))
        if kind == "nativo" and lo != owner and va(row["va_coverage"]) == lo:
            require(lo not in fragments, f"Duplicate fragment: {fmt(lo)}")
            fragments[lo] = owner
    for owner, ranges in spans.items():
        partition = sorted((lo, hi) for lo, hi, kind in ranges if kind != "marcador")
        require(partition[0][0] == owner and all(a[1] == b[0] for a, b in zip(partition, partition[1:])),
                f"Noncontiguous or overlapping spans: {fmt(owner)}")
        for lo, hi, kind in ranges:
            if kind == "marcador":
                require(any(a <= lo < hi <= b and subkind == "nativo" for a, b, subkind in ranges),
                        f"Marker outside native span: {fmt(lo)}")
    return spans, fragments


def mode_for(entry: int, body: list[Instruction], row: dict, matched_size: int,
             spans: dict, known_islands: set[int], vm_sites: set[int], setup_calls: set[int]) -> tuple[str, str]:
    if {"nativo", "vm"} <= {kind for _, _, kind in spans.get(entry, [])}:
        return "mixed", "docs/vm_spans.tsv; boundaries checked against original ASM and PE"
    if entry in known_islands:
        return "mixed", "original-relations.tsv; native bypass and VM transfer checked against original ASM"
    if not body:
        return "undetermined", "No Ghidra body; no claim about the absence of VM"
    edges = [(i, i.direct()) for i in body if i.op.startswith(("J", "CALL", "LOOP"))]
    contiguous = all(a.end == b.address for a, b in zip(body, body[1:]))
    setup = {"PUSH", "MOV", "SUB", "LEA", "ADD"}
    if (
        body[0].address == entry
        and contiguous
        and body[-1].end - entry == matched_size
        and body[-1].op == "JMP"
        and body[-1].direct() in vm_sites
        and all(i.op in setup or (i.op == "CALL" and i.direct() in setup_calls) for i in body[:-1])
    ):
        return "native_vm_entry", "Observed native setup transfers to a cataloged VM site"
    if any(i.address in vm_sites for i in body) or any(target in vm_sites for _, target in edges):
        return "undetermined", "Known VM boundary; body extent not established"
    complete = (
        row["extent"] == "contiguo"
        and contiguous
        and body[0].address == entry
        and body[-1].end - entry == matched_size == int(row["original_body_bytes"])
        and any(i.op.startswith("RET") for i in body)
    )
    starts = {i.address for i in body}
    unresolved_jump = any(not i.op.startswith("CALL") and target not in starts for i, target in edges)
    if complete and not unresolved_jump:
        return "no_vm_boundary_observed", "Matching contiguous ASM with a return and resolved in-body jumps"
    return "undetermined", "Body extent, exit, or jump targets not established"


def origin(row: dict, classification: str) -> str:
    bucket, name = row["bucket"], row["reconstructed_function"].casefold()
    if classification == "protection_excluded" or bucket in {"proteccion", "crack"}:
        return "protection_excluded"
    if bucket == "generado":
        return "cataloged_stl_instantiation" if "stl" in row["subsistema"].casefold() else "cataloged_generated"
    if bucket == "mfc_crt":
        return "cataloged_static_library"
    if "deleting dtor" in name or "deleting destructor" in name:
        return "cataloged_compiler_support"
    return "cataloged_application"


def jump_shape(address: int, raw: bytes, imports: dict[int, str]) -> tuple[str, str]:
    if raw[:2] == b"\xff\x25":
        key = struct.unpack_from("<I", raw, 2)[0]
        if key in imports:
            return "iat_jump", imports[key]
        return "indirect_unresolved", ""
    if raw[0] == 0xE9:
        return "relative_jump", fmt(address + 5 + struct.unpack_from("<i", raw, 1)[0])
    return "unrecognized_jump", ""


def research_sources(root: Path, rea_evidence: Path) -> dict[str, Path]:
    base = root / "mydll-gameserver/decompilacion-completa"
    reconstruction = base / "reconstruction"
    contracts = reconstruction / "cleanroom_t9/contracts"
    supplemental = reconstruction / "evidence/analysis_inputs"
    return {
        "original_dll": root / "artifacts/MyDll_original.dll",
        "installed_dll": base / "dynamic/MyDll_inuse_8b39364e.dll",
        "coverage": reconstruction / "coverage.tsv",
        "ledger": contracts / "ORIGINAL_FEATURE_LEDGER.tsv",
        "spans": reconstruction / "docs/vm_spans.tsv",
        "original_trampolines": base / "devirt/entries/entries.csv",
        "original_trampoline_attributions": contracts / "ORIGINAL_TRAMPOLINE_FEATURE_MAP.tsv",
        "archived_ghidra": base / "static/functions.json",
        "rea": rea_evidence,
        "relations": COVERAGE_DIR / "original-relations.tsv",
        "generator": Path(__file__),
        "citywar_contract": contracts / "VM_ISLA_10019898.md",
        "entry_manifest": contracts / "ENTRY_MANIFEST.md",
        "init_continuation_asm": supplemental / "0x10002212.asm",
        "additional_callback_asm": supplemental / "0x1002b5cb.asm",
    }


@dataclass(frozen=True)
class Relations:
    spans: dict[int, list[tuple[int, int, str]]]
    fragments: dict[int, int]
    mixed_islands: set[int]
    additional_entries: set[int]
    regions: dict[str, dict[str, str]]
    vm_sites: set[int]


def checked_relations(sources: dict[str, Path], original: Image, cov: dict, ledger: dict,
                      bodies: dict[int, list[Instruction]], stubs: dict[int, str]) -> Relations:
    spans, fragments = read_spans(sources["spans"])
    for source in ("init_continuation_asm", "additional_callback_asm"):
        path = sources[source]
        entry = va(path.stem)
        verify_objdump(path, entry, int(cov[entry]["original_body_bytes"]), original)
    relationships = table(sources["relations"])
    known_islands = set()
    extra_entries = set()
    region_links = {}
    vm_sites = {*stubs, DISPATCHER}
    for base_va, rows in spans.items():
        require(base_va in cov, f"Span parent missing from coverage: {fmt(base_va)}")
        for lo, hi, kind in rows:
            require(len(original.read(lo, hi - lo)) == hi - lo, f"Span outside file: {fmt(lo)}")
            if kind == "vm":
                vm_sites.add(lo)
            if kind != "marcador":
                require(not any(i.address < x < i.end for i in bodies[base_va] for x in (lo, hi)),
                        f"Span boundary splits an instruction: {fmt(lo)}")
    for rel in relationships:
        parent, kind = va(rel["parent_va"]), rel["kind"]
        require(parent in cov, f"Relation parent missing from coverage: {fmt(parent)}")
        if kind == "fragment":
            sub = va(rel["subject"])
            require(sub not in fragments and sub in cov, f"Invalid fragment: {fmt(sub)}")
            fragments[sub] = parent
        elif kind == "additional_entry":
            require(va(rel["subject"]) == parent, "Additional root and entry differ")
            reference = va(rel["related_va"])
            require(original.read(reference, 6) == b"\x8d\x05" + struct.pack("<I", parent),
                    f"Missing native installation reference: {fmt(parent)}")
            extra_entries.add(parent)
        elif kind == "mixed_island":
            sub, end = va(rel["subject"]), va(rel["related_va"])
            code = {i.address: i for i in bodies[parent]}
            region_entries = {va(x["related_va"]) for x in relationships if x["kind"] == "region"}
            require(sub in code and sub in vm_sites | region_entries, f"Island lacks a cataloged entry: {fmt(sub)}")
            require(end in code and any(i.address < sub and i.op == "CALL" for i in code.values())
                    and any(i.address > end and i.op.startswith("RET") for i in code.values()),
                    f"Island lacks native code on both sides: {fmt(sub)}")
            require(any(i.address < sub and i.op.startswith("J") and i.op != "JMP" and i.direct() == end
                        for i in code.values()), f"Island lacks a native bypass branch: {fmt(sub)}")
            require(any(i.op == "JMP" and sub <= i.address < end and i.direct() in stubs for i in code.values()),
                    f"Island lacks a jump to a cataloged VM stub: {fmt(sub)}")
            known_islands.add(parent)
        elif kind == "region":
            name, start = rel["subject"], va(rel["related_va"])
            require(name not in region_links and ledger[name]["subject_kind"] == "vm_region", f"Invalid region: {name}")
            if "vm_spans.tsv" in rel["evidence"]:
                require(any(lo == start and flag == "vm" for lo, _, flag in spans.get(parent, [])),
                        f"VM anchor has no matching span: {name}")
            region_links[name] = rel
            vm_sites.add(start)
        else:
            raise ValueError(f"Unknown relation: {kind}")
    require(region_links.keys() == {k for k, r in ledger.items() if r["subject_kind"] == "vm_region"},
            "Ledger and region table disagree")
    return Relations(spans, fragments, known_islands, extra_entries, region_links, vm_sites)


def index_control_transfers(bodies: dict[int, list[Instruction]]) -> tuple[dict[int, set[int]], dict[int, set[int]]]:
    direct_calls: defaultdict[int, set[int]] = defaultdict(set)
    direct_transfers: defaultdict[int, set[int]] = defaultdict(set)
    for owner, code in bodies.items():
        for instruction in code:
            target = instruction.direct()
            if target is not None:
                direct_transfers[target].add(owner)
                if instruction.op == "CALL":
                    direct_calls[target].add(owner)
    return direct_calls, direct_transfers


def vm_tables(sources: dict[str, Path], original: Image, installed: Image, stubs: dict[int, str],
              installed_stubs: dict[int, str], ledger: dict, region_links: dict) -> tuple[list[dict], list[dict]]:
    old = index(table(sources["original_trampolines"], ","), "stub_va", True)
    claims = index(table(sources["original_trampoline_attributions"]), "stub_va", True)
    require(stubs.keys() == old.keys() == claims.keys(), "Trampoline address sets differ")
    require(all(va(old[k]["selector_va"]) == va(r["selector"]) for k, r in claims.items()),
            "Historical selector catalogs disagree")
    require(all(stubs[a] == old[a]["stub_section"] for a in stubs), "Trampoline sections differ")
    region_by_anchor = {va(r["related_va"]): name for name, r in region_links.items()}
    require(len(region_by_anchor) == len(region_links), "Duplicate region anchor")
    linked_tramps = []
    for address, claim in sorted(claims.items()):
        proof = claim["region_evidence"]
        region = region_by_anchor.get(va(claim["region"])) if claim["region"] != "?" else None
        # The F5–F9 subregions share one feature relation, not five functions.
        if proof == "static_closure_edge" and not region:
            matches = [name for name in region_links if ledger[name]["feature_id"] == claim["feature_id"]]
            region = matches[0] if len(matches) == 1 else None
        owner = region_links[region]["parent_va"] if region and proof == "static_closure_edge" else ""
        require(proof != "static_closure_edge" or bool(owner), f"Edge lacks a parent: {fmt(address)}")
        require(not owner or owner == claim["root_native"].partition(":")[0],
                f"Parent disagrees with the edge catalog: {fmt(address)}")
        linked_tramps.append({
            "va": fmt(address), "stub_section": stubs[address],
            "cataloged_selector_section": old[address]["bytecode_section"],
            "region_or_candidate": region or claim["region"], "cataloged_attribution": proof,
            "attributed_parent_va": owner,
            "attributed_feature": claim["feature_id"] if owner else "",
            "matches_installed": int(
                address in installed_stubs and original.read(address, 10) == installed.read(address, 10)),
        })
    regions = []
    for name, rel in region_links.items():
        lower = rel["related_va"]
        regions.append({
            "region_id": name, "parent_va": rel["parent_va"], "anchor_va": lower,
            "feature": ledger[name]["feature_id"],
            "edge_attributed_trampolines": sum(
                r["region_or_candidate"] == name and bool(r["attributed_parent_va"]) for r in linked_tramps),
            "evidence": rel["evidence"],
        })
    return regions, linked_tramps


def load_indexes(sources: dict[str, Path]) -> tuple[dict, dict, dict, dict, dict]:
    historical = json.loads(sources["archived_ghidra"].read_text(encoding="utf-8"))
    require(historical["report"]["target_sha256"] == ORIGINAL, "Ghidra export does not match the original")
    ghidra = index(historical["functions"], "address", True)
    bundle = json.loads(sources["rea"].read_text(encoding="utf-8"))
    records = [r for r in bundle["records"] if r["operation"] == "list_procedures"]
    require(len(records) == 1, "Expected exactly one REA procedure listing")
    rea = records[0]
    require(rea["subject"]["digest"]["sha256"] == ORIGINAL, "REA evidence does not match the original")
    live_ghidra = index([r for r in rea["raw_result"] if not r["procedure"]["external"]], "address", True)
    cov = index(table(sources["coverage"]), "original_va", True)
    ledger = index(table(sources["ledger"]), "subject_id")
    require({r["bucket"] for r in cov.values()} <= BUCKETS, "Unknown original coverage bucket")
    require({va(k) for k, r in ledger.items() if r["subject_kind"] == "native"} == cov.keys(),
            "Coverage and ledger address sets differ")
    require({r["subject_kind"] for r in ledger.values()} <= {"native", "vm_region", "table", "external"},
            "Unknown ledger subject kind")
    return rea, ghidra, live_ghidra, cov, ledger


def main() -> None:
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--research-root", required=True, type=Path)
    args.add_argument("--rea-evidence", required=True, type=Path)
    args.add_argument("--out", required=True, type=Path)
    config = args.parse_args()
    sources = research_sources(config.research_root, config.rea_evidence)
    original = Image(sources["original_dll"], ORIGINAL)
    installed = Image(sources["installed_dll"], INSTALLED)
    rea, ghidra, live_ghidra, cov, ledger = load_indexes(sources)
    bodies, instruction_owners, asm_hashes = read_asm(sources["archived_ghidra"].parent, ghidra, original)
    setup_calls = {
        address for address, row in cov.items()
        if row["bucket"] == "mfc_crt" and row["reconstructed_function"] == "mydll_crt_memset"
        and ghidra.get(address, {}).get("name") == "_memset"
        and any(i.op == "STOSD.REP" for i in bodies[address])
        and not any(i.op.startswith("CALL") for i in bodies[address])
    }
    stubs = original.stubs()
    stubs_installed = installed.stubs()
    handlers = original.handler_slots()
    require(original.section(DISPATCH_TABLE) == ".text" and all(original.executable(a) for a in handlers),
            "Cataloged dispatch table has a target outside executable sections")
    relations = checked_relations(sources, original, cov, ledger, bodies, stubs)
    direct_calls, direct_transfers = index_control_transfers(bodies)
    regions, linked_tramps = vm_tables(sources, original, installed, stubs, stubs_installed, ledger, relations.regions)
    physical = []
    logical = []
    compiler = []
    missing_calls = set(direct_calls) - cov.keys()
    all_addresses = cov.keys() | ghidra.keys() | live_ghidra.keys() | stubs.keys() | handlers.keys() | missing_calls
    for address in sorted(all_addresses):
        row = cov.get(address)
        led = ledger.get(fmt(address), {})
        owner_list = instruction_owners.get(address, set()) - {address}
        bucket = "" if row is None else row["bucket"]
        body = bodies.get(address, [])
        pe_origin = ""
        specific = ""
        count_as_function = False
        if address in stubs:
            kind = "vm_trampoline"
        elif address in handlers:
            kind = "vm_dispatch_target"
        elif address in relations.fragments:
            kind = "fragment"
            specific = fmt(relations.fragments[address])
            if direct_calls.get(address, set()) - {relations.fragments[address]}:
                kind = "fragment_entry_needs_review"
        elif row and row["status"] in {"no_funcion", "declarada"}:
            kind = "label_or_excluded_code"
        elif bucket == "vm":
            kind = "cataloged_vm_support"
        elif bucket == "thunk" or (address in missing_calls and original.read(address, 1) == b"\xe9"):
            kind = "jump_site"
            pe_origin, specific = jump_shape(address, original.read(address, 6), original.imports)
        elif row and row["extent"].startswith("fragmento_de:"):
            kind = "cataloged_fragment"
            specific = row["extent"].partition(":")[2]
        elif (address in ghidra or address in relations.additional_entries) and not owner_list and original.executable(address):
            kind, count_as_function = "procedure_candidate", True
        else:
            kind = "undelimited_entry"
        physical.append({
            "va": fmt(address), "rva": fmt(address - original.base), "section": original.section(address),
            "kind": kind, "detail": specific, "pe_mechanism": pe_origin,
            "historical_bucket": bucket, "archived_ghidra": int(address in ghidra), "rea_ghidra": int(address in live_ghidra),
            "containing_body_entries": ";".join(fmt(x) for x in sorted(owner_list)),
            "direct_caller_count": len(direct_calls.get(address, ())),
            "direct_transfer_body_entries": ";".join(fmt(x) for x in sorted(direct_transfers.get(address, ()))),
            "vm_dispatch_slots": handlers.get(address, 0),
        })
        if count_as_function:
            category = origin(row, led.get("classification", "")) if row else "unknown_origin"
            mode, evidence = ("not_applicable", "")
            if row and category == "cataloged_application":
                mode, evidence = mode_for(address, body, row, ghidra.get(address, {}).get("body_size", 0),
                                          relations.spans, relations.mixed_islands, relations.vm_sites, setup_calls)
            logical.append({
                "function_id": f"mydll!{fmt(address - original.base)}", "va": fmt(address),
                "analysis_name": row["reconstructed_function"] if row and row["reconstructed_function"] != "-"
                else ghidra.get(address, {}).get("name", ""),
                "cataloged_origin": category, "body_mode": mode, "mode_evidence": evidence,
                "entry_evidence": f"static/asm/{fmt(address)}.asm" if body
                else f"evidence/analysis_inputs/{fmt(address)}.asm; native reference in Init",
                "decoded_bytes": sum(len(i.code) for i in body) if body else int(row["original_body_bytes"]),
                "feature_id": led.get("feature_id", ""), "ledger_classification": led.get("classification", ""),
            })
        if kind == "jump_site" or bucket in {"generado", "mfc_crt"} or (
            row and origin(row, "") == "cataloged_compiler_support"
        ):
            compiler.append({
                "va": fmt(address), "cataloged_role": origin(row, "") if row and kind != "jump_site" else "unknown_origin",
                "analysis_name": row["reconstructed_function"] if row else "",
                "subsystem": row["subsistema"] if row else "",
                "address_kind": kind, "catalog_status": row["status"] if row else "",
                "evidence": f"coverage.tsv#{address:08x}" if row else "Direct jump inside a byte-checked Ghidra body",
            })
    own = [r for r in logical if r["cataloged_origin"] == "cataloged_application"]
    require({r["parent_va"] for r in regions} <= {r["va"] for r in own}, "Region parent is not an application candidate")
    other_subjects = [
        {k: r[k] for k in ("subject_id", "subject_kind", "classification", "feature_id", "evidence_ids")}
        for r in ledger.values() if r["subject_kind"] in {"table", "external"}
    ]
    config.out.mkdir(parents=True, exist_ok=False)
    output = {
        "original-addresses.tsv": physical, "original-functions.tsv": logical,
        "original-vm-regions.tsv": regions, "original-vm-trampolines.tsv": linked_tramps,
        "original-toolchain.tsv": compiler, "original-other-subjects.tsv": other_subjects,
    }
    for name, rows in output.items():
        emit(config.out / name, rows)
    diff_count = sum(a != b for a, b in zip(original.pe.__data__, installed.pe.__data__))
    text_sections = [s for s in original.pe.sections if s.Name.startswith(b".text\0")]
    require(len(text_sections) == 1, "Expected exactly one .text section")
    text_rva = text_sections[0].VirtualAddress
    text_raw = text_sections[0].SizeOfRawData
    text_start = original.base + text_rva
    decoded_text = {offset for body in bodies.values() for i in body for offset in range(i.address, i.end)
                    if text_start <= offset < text_start + text_raw}
    asm_inventory = "".join(f"{digest}  {name}\n" for name, digest in sorted(asm_hashes.items()))
    summary = {
        "pefile": pefile.__version__, "rea_evidence_id": rea["evidence_id"], "rea_profile": rea["analysis_profile"],
        "inputs": {k: sha(p) for k, p in sources.items()},
        "archived_asm_inventory_sha256": hashlib.sha256(asm_inventory.encode("utf-8")).hexdigest(),
        "outputs": {name: sha(config.out / name) for name in output},
        "counts": {
            "coverage": len(cov), "ledger": len(ledger), "address_union": len(physical),
            "archived_ghidra": len(ghidra), "rea_ghidra": len(live_ghidra),
            "rea_externals": len(rea["raw_result"]) - len(live_ghidra),
            "archived_ghidra_only": sorted(map(fmt, ghidra.keys() - live_ghidra.keys())),
            "rea_ghidra_only": sorted(map(fmt, live_ghidra.keys() - ghidra.keys())),
            "coverage_only": len(cov.keys() - ghidra.keys()), "ghidra_only_vs_coverage": len(ghidra.keys() - cov.keys()),
            "region_relations": len(regions), "procedure_candidates": len(logical),
            "application_body_modes": Counter(r["body_mode"] for r in own),
            "candidate_origins": Counter(r["cataloged_origin"] for r in logical),
            "address_kinds": Counter(r["kind"] for r in physical),
            "direct_call_targets_outside_coverage": sorted(map(fmt, missing_calls)),
            "original_trampoline_sections": Counter(stubs.values()),
            "installed_trampoline_sections": Counter(stubs_installed.values()),
            "trampoline_attributions": Counter(r["cataloged_attribution"] for r in linked_tramps),
            "matching_trampoline_windows": sum(r["matches_installed"] for r in linked_tramps),
            "vm_dispatch_targets": len(handlers), "vm_dispatch_slots": sum(handlers.values()),
            "matching_vm_dispatch_table": original.read(DISPATCH_TABLE, 1024) == installed.read(DISPATCH_TABLE, 1024),
            "file_size_original": len(original.pe.__data__), "file_size_installed": len(installed.pe.__data__),
            "changed_bytes_in_common_prefix": diff_count,
            "verified_asm_bytes_in_text": len(decoded_text), "raw_text_bytes": text_raw,
            "verified_ghidra_instructions": sum(len(body) for body in bodies.values()),
            "coverage_only_buckets": Counter(cov[a]["bucket"] for a in cov.keys() - ghidra.keys()),
            "jump_mechanisms": Counter(r["pe_mechanism"] for r in physical if r["kind"] == "jump_site"),
            "generated_catalog_status": Counter(r["status"] for r in cov.values() if r["bucket"] == "generado"),
            "vm_dispatch_table_sha256": hashlib.sha256(original.read(DISPATCH_TABLE, 1024)).hexdigest(),
            "original_imports": len(original.imports),
            "original_exports": [(x.name.decode("ascii"), x.ordinal, fmt(x.address))
                                 for x in original.pe.DIRECTORY_ENTRY_EXPORT.symbols],
            "section_alignment_original": original.pe.OPTIONAL_HEADER.SectionAlignment,
            "file_alignment_original": original.pe.OPTIONAL_HEADER.FileAlignment,
        },
    }
    manifest = json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True)
    (config.out / "original-manifest.json").write_text(manifest + "\n", encoding="utf-8")
    print(json.dumps(summary["counts"], ensure_ascii=False, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
