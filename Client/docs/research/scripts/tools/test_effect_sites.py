"""Regresiones del censo crudo CL11.R; no necesitan el exe ni dependencias."""
import io
import os
import sys
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import effect_sites


class EffectSitesTest(unittest.TestCase):
    def test_filters_sections_indirect_calls_and_preserves_phases(self):
        listing = io.StringIO('''Disassembly of section .data:
  401000: call   0x518cf0
Disassembly of section .text:
  401020: call   0x52e590
  401010: call   0x518cf0
  401030: call   DWORD PTR ds:0x518cf0
  401040: call   0x52e500
  401050: call   0x52e470
Disassembly of section .rdata:
  401060: call   0x518cf0
''')
        self.assertEqual(effect_sites.scan_sites(listing), [
            (0x401010, 0x518cf0, 'modelo', 'alta/actualización'),
            (0x401020, 0x52e590, 'cintas', 'dibujo'),
            (0x401040, 0x52e500, 'cintas', 'paso'),
            (0x401050, 0x52e470, 'cintas', 'alta/actualización'),
        ])

    def assert_cli_rejects(self, listing):
        files = iter((io.StringIO(effect_sites.C32_SHA256), io.StringIO(listing)))
        output = io.StringIO()
        with mock.patch('builtins.open', side_effect=lambda *args, **kwargs: next(files)):
            with mock.patch('sys.stdout', output):
                with self.assertRaises(SystemExit):
                    effect_sites.main()
        self.assertEqual(output.getvalue(), '')

    def test_cli_rejects_missing_text_truncation_and_raw_bytes(self):
        for listing in ('', '  401000: call   0x518cf0\n',
                        'Disassembly of section .text:\n  401000: call   0x518cf0\n',
                        'Disassembly of section .text:\n  401000: e8 eb 7c 11 00 call 0x518cf0\n'):
            with self.subTest(listing=listing):
                self.assert_cli_rejects(listing)

    def test_cli_rejects_duplicate_va_even_with_expected_counts(self):
        rows = ['Disassembly of section .text:']
        for target, count in effect_sites.EXPECTED_COUNTS.items():
            rows.extend('  401000: call   0x%x' % target for _ in range(count))
        self.assert_cli_rejects('\n'.join(rows))

    def test_rejects_other_or_missing_digest(self):
        for source in ('', 'bad  /ruta/main.exe\n'):
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    effect_sites.validate_source(source)
        effect_sites.validate_source(effect_sites.C32_SHA256 + '  /ruta/main.exe\n')


if __name__ == '__main__':
    unittest.main()
