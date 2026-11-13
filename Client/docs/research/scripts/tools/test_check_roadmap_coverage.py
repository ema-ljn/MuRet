#!/usr/bin/env python3
"""Controles negativos del verificador de CL.M (check_roadmap_coverage.py).

Parte de la matriz del repo, le hace un cambio por caso en una copia temporal y comprueba que el
verificador lo rechace (o lo acepte, cuando el caso es válido). El inventario sale de la variable
MURET_MAINEXE_INVENTORY (la ruta al inventory.tsv del research, scripts/README.md, El entorno).

    MURET_MAINEXE_INVENTORY="$RESEARCH/client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv" \\
        python3 -I Client/docs/research/scripts/tools/test_check_roadmap_coverage.py
"""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_roadmap_coverage as C  # noqa: E402

MAPPING = os.path.join(HERE, '..', '..', 'coverage', 'mainexe-roadmap.tsv')
INVENTORY = os.environ.get('MURET_MAINEXE_INVENTORY', '')


@unittest.skipUnless(os.path.isfile(INVENTORY), 'falta MURET_MAINEXE_INVENTORY')
class CheckRoadmapCoverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(MAPPING, encoding='utf-8') as fh:
            cls.lines = fh.read().split('\n')
        cls.header = cls.lines[0].split('\t')
        cls.col = {name: i for i, name in enumerate(cls.header)}

    def rows(self):
        return [ln.split('\t') for ln in self.lines[1:] if ln]

    def first(self, rows, **want):
        for i, r in enumerate(rows):
            if all(r[self.col[k]] == v if not callable(v) else v(r[self.col[k]]) for k, v in want.items()):
                return i
        self.fail(f'no hay una fila con {want}')

    def run_check(self, rows, require_complete=False):
        with tempfile.NamedTemporaryFile('w', suffix='.tsv', delete=False, encoding='utf-8') as fh:
            fh.write('\t'.join(self.header) + '\n')
            for r in rows:
                fh.write('\t'.join(r) + '\n')
            path = fh.name
        try:
            errors, _ = C.check(INVENTORY, path, require_complete)
        finally:
            os.unlink(path)
        return errors

    def test_the_repo_matrix_is_valid_and_complete(self):
        self.assertEqual(self.run_check(self.rows()), [])
        self.assertEqual(self.run_check(self.rows(), require_complete=True), [])

    def test_a_repeated_va_fails(self):
        rows = self.rows()
        rows.append(list(rows[10]))
        self.assertTrue(any('repetida' in e for e in self.run_check(rows)))

    def test_a_foreign_va_with_the_same_row_count_fails(self):
        rows = self.rows()
        rows[5][0] = '00400000'
        errors = self.run_check(rows)
        self.assertTrue(any('no está en el inventario' in e for e in errors))
        self.assertTrue(any('falta la VA' in e for e in errors))

    def test_a_changed_size_fails(self):
        rows = self.rows()
        rows[7][1] = str(int(rows[7][1]) + 1)
        self.assertTrue(any('size_bytes' in e for e in self.run_check(rows)))

    def test_a_game_row_without_wave_fails(self):
        rows = self.rows()
        i = self.first(rows, classification='game')
        rows[i][self.col['primary_wave']] = ''
        self.assertTrue(any('ola responsable' in e for e in self.run_check(rows)))

    def test_cl12_fails(self):
        rows = self.rows()
        i = self.first(rows, classification='game')
        rows[i][self.col['primary_wave']] = 'CL12'
        self.assertTrue(any('ola inválida' in e for e in self.run_check(rows)))

    def test_a_row_without_evidence_fails(self):
        rows = self.rows()
        i = self.first(rows, classification='library')
        rows[i][self.col['evidence']] = ''
        self.assertTrue(any('evidencia' in e for e in self.run_check(rows)))

    def test_a_pending_row_passes_only_without_require_complete(self):
        rows = self.rows()
        i = self.first(rows, classification='game')
        for k, v in (('classification', 'pending'), ('primary_wave', ''), ('consumer_waves', ''),
                     ('analysis_state', 'pending'), ('implementation_state', 'unknown'),
                     ('next_action', 'leer el .asm y buscar quién la alcanza')):
            rows[i][self.col[k]] = v
        self.assertEqual(self.run_check(rows), [])
        self.assertTrue(any('require-complete' in e for e in self.run_check(rows, require_complete=True)))

    def test_a_thunk_without_wave_and_a_game_row_with_two_consumers_pass(self):
        rows = self.rows()
        i = self.first(rows, classification='thunk', primary_wave='')
        j = self.first(rows, classification='game', consumer_waves=lambda v: v.count(';') >= 1)
        self.assertTrue(rows[i] and rows[j])
        self.assertEqual(self.run_check(rows), [])

    def test_a_library_row_with_a_wave_fails(self):
        rows = self.rows()
        i = self.first(rows, classification='library')
        rows[i][self.col['primary_wave']] = 'CL3'
        self.assertTrue(any('sin ola' in e for e in self.run_check(rows)))


if __name__ == '__main__':
    unittest.main()
