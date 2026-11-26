import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from generate_original_coverage import COVERAGE_DIR, Instruction, index, jump_shape, mode_for, read_spans, sha, table


class OriginalCoverageTest(unittest.TestCase):
    def classify(self, instructions, size, vm_sites=(), setup_calls=()):
        row = {"extent": "contiguo", "original_body_bytes": str(size)}
        return mode_for(0x100, instructions, row, size, {}, set(), set(vm_sites), set(setup_calls))[0]

    def test_native_return_is_not_mixed(self):
        body = [Instruction(0x100, b"\xc3", "RET")]
        self.assertEqual(self.classify(body, 1), "no_vm_boundary_observed")

    def test_jump_to_vm_with_native_return_is_not_a_wrapper(self):
        body = [Instruction(0x100, b"\xc3", "RET"), Instruction(0x101, b"\xe9\xfa\x00\x00\x00", "JMP")]
        self.assertEqual(self.classify(body, 6, [0x200]), "undetermined")

    def test_wrapper_can_initialize_locals_with_a_known_crt_call(self):
        body = [Instruction(0x100, b"\xe8\x7b\x00\x00\x00", "CALL"),
                Instruction(0x105, b"\xe9\xf6\x00\x00\x00", "JMP")]
        self.assertEqual(self.classify(body, 10, [0x200], [0x180]), "native_vm_entry")

    def test_uncataloged_setup_call_prevents_wrapper_classification(self):
        body = [Instruction(0x100, b"\xe8\x7b\x00\x00\x00", "CALL"),
                Instruction(0x105, b"\xe9\xf6\x00\x00\x00", "JMP")]
        self.assertEqual(self.classify(body, 10, [0x200]), "undetermined")

    def test_indirect_jump_does_not_establish_native_mode(self):
        body = [Instruction(0x100, b"\xff\xe0", "JMP"), Instruction(0x102, b"\xc3", "RET")]
        self.assertEqual(self.classify(body, 3), "undetermined")

    def test_jump_outside_iat_is_unresolved(self):
        encoded = b"\xff\x25" + struct.pack("<I", 0x400)
        self.assertEqual(jump_shape(0x100, encoded, {})[0], "indirect_unresolved")

    def test_iat_jump_resolves_to_the_import(self):
        encoded = b"\xff\x25" + struct.pack("<I", 0x400)
        self.assertEqual(jump_shape(0x100, encoded, {0x400: "KERNEL32!Sleep"}), ("iat_jump", "KERNEL32!Sleep"))

    def test_numeric_aliases_cannot_duplicate_an_entry(self):
        with self.assertRaisesRegex(ValueError, "Duplicate key"):
            index([{"va": "1000"}, {"va": "0x1000"}], "va", True)

    def test_overlapping_base_spans_are_rejected(self):
        payload = (
            "va_funcion\tlo\thi\ttipo\tva_coverage\n"
            "0100\t0100\t0120\tnativo\t0100\n"
            "0100\t011f\t0130\tvm\t0100\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "spans.tsv"
            p.write_text(payload, encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "overlapping"):
                read_spans(p)

    def test_tracked_snapshots_match_the_manifest(self):
        with (COVERAGE_DIR / "original-manifest.json").open(encoding="utf-8") as file:
            expected = json.load(file)["outputs"]
        actual = {name: sha(COVERAGE_DIR / name) for name in expected}
        self.assertEqual(actual, expected)

    def test_roadmap_anchors_resolve_to_application_entries(self):
        entries = {
            row["function_id"] for row in table(COVERAGE_DIR / "original-functions.tsv")
            if row["cataloged_origin"] == "cataloged_application"
        }
        roadmap = table(COVERAGE_DIR / "port-roadmap.tsv")
        self.assertEqual({row["original_function_id"] for row in roadmap} - entries, set())

    def test_roadmap_dependencies_precede_their_work_items(self):
        earlier = {""}
        for row in table(COVERAGE_DIR / "port-roadmap.tsv"):
            self.assertIn(row["depends_on"], earlier)
            earlier.add(row["work_item"])


if __name__ == "__main__":
    unittest.main()
