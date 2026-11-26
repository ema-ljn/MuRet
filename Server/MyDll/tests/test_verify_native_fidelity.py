import csv
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import verify_native_fidelity as fidelity

ARTIFACTS = Path("/tmp/opencode/muret-native-port.PnVSjA/artifacts")


class NativeVerifierTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.work = Path(self.temp.name)
        self.dll = self.work / "MyDll.dll"
        self.link_map = self.work / "MyDll.map"
        shutil.copy(ARTIFACTS / "MyDll.dll", self.dll)
        shutil.copy(ARTIFACTS / "MyDll.map", self.link_map)
        self.original = self.work / "original.dll"
        self.installed = self.work / "installed.dll"
        shutil.copy("/home/emaaljn/re-muserver-mydll-excluidas-instr21/artifacts/MyDll_original.dll", self.original)
        shutil.copy(
            "/home/emaaljn/re-muserver-mydll-excluidas-instr21/mydll-gameserver/decompilacion-completa/"
            "dynamic/MyDll_inuse_8b39364e.dll",
            self.installed,
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_linked_bytes_and_ledger_are_verified(self):
        result = fidelity.compare(self.dll, self.link_map, self.original, self.installed)
        self.assertEqual(len(result), 5)
        fidelity.check_ledger(Path(__file__).parents[1] / "research/native-functions.tsv")

    def test_a_changed_linked_byte_is_rejected(self):
        symbols, _, _ = fidelity.read_map(self.link_map)
        data = bytearray(self.dll.read_bytes())
        import pefile
        with pefile.PE(str(self.dll)) as pe:
            offset = pe.get_offset_from_rva(symbols[fidelity.NATIVE[0x10004B4F][0]])
        data[offset] ^= 1
        self.dll.write_bytes(data)
        with self.assertRaisesRegex(fidelity.FidelityError, "Linked bytes differ"):
            fidelity.compare(self.dll, self.link_map, self.original, self.installed)

    def test_a_premature_exact_ledger_row_is_rejected(self):
        ledger = self.work / "native.tsv"
        with (Path(__file__).parents[1] / "research/native-functions.tsv").open() as source:
            rows = list(csv.reader(source, delimiter="\t"))
        rows[1][5:9] = ["exact", "not_applicable", "pending", "not_linked"]
        ledger.write_text("\n".join("\t".join(row) for row in rows) + "\n")
        with self.assertRaisesRegex(fidelity.FidelityError, "Ledger status differs"):
            fidelity.check_ledger(ledger)


if __name__ == "__main__":
    unittest.main()
